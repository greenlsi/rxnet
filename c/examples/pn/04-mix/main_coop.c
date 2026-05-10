// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * PN 03-mix — cooperative (single-thread) multi-rate scheduler.
 *
 * Each net (and the CLI machine) carries its own period; rx_coop_exec
 * tracks the next activation instant for every periodic node:
 *
 *   pn_rt:  light_a 10 ms, blink_b 10 ms, auto_c 20 ms  [rx_pn_runtime]
 *             next activations at 10 ms / 20 ms periods
 *   cli_rt: cli     10 ms                                 [rx_fsm_runtime]
 *
 * rx_coop_exec runs whichever runtime is due, then sleeps until the
 * nearest upcoming deadline — no preemption, no OS threads.
 *
 * Contrast with main_cli.c (cyclic executive):
 *   - Cyclic exec: pre-computed static dispatch table.
 *   - Coop exec: dynamic deadline scheduling; tasks that run long do
 *     not corrupt the table — the scheduler just catches up on the
 *     next iteration.
 *
 * Contrast with main_threads.c:
 *   - Single thread: no concurrent access, no mutexes needed.
 *   - Suitable for cooperative RTOS (e.g. FreeRTOS taskYIELD pattern).
 *
 * Build:
 *   make -C c pn_03_mix_coop
 *   ./c/build/pn_03_mix_coop
 */

#include <stdio.h>
#include <stdlib.h>

#include "rxnet/coop.h"
#include "rxnet/fsm.h"
#include "rxnet/pn.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "light_pn.h"
#include "auto_pn.h"
#include "blink_pn.h"
#include "sched_report.h"

#define LIGHT_A_GPIO         2
#define LIGHT_B_GPIO         4
#define LIGHT_C_GPIO         5
#define BUTTON_A_GPIO        0
#define BUTTON_B_GPIO        15
#define FAST_PERIOD_US       10000L
#define SLOW_PERIOD_US       20000L
#define CLI_PERIOD_US        10000L
#define DEFAULT_FREQ_B_HZ    2u
#define DEFAULT_TIMEOUT_C_MS 9000u

typedef struct {
    rx_pn_net *light_a;
    rx_pn_net *blink_b;
    rx_pn_net *auto_c;
} app_data;

/* ------------------------------------------------------------------ */
/* CLI commands                                                         */
/* ------------------------------------------------------------------ */

static const char *light_state(const rx_pn_net *net, int p_on) {
    return (net != NULL && net->places[p_on] > 0) ? "ON" : "OFF";
}

static const char *blink_state(const rx_pn_net *net) {
    if (net == NULL) return "OFF";
    if (net->places[2] > 0) return "X2";
    if (net->places[1] > 0) return "X1";
    return "OFF";
}

static void
cmd_button_a(rx_fsm_context *ctx, cli_machine_data *cli,
             const char *command, void *ud)
{
    (void)ctx; (void)cli; (void)command; (void)ud;
    if (app_driver_trigger_button(BUTTON_A_GPIO) == ESP_OK) { printf("button A queued\n"); return; }
    printf("button A trigger failed\n");
}

static void
cmd_button_b(rx_fsm_context *ctx, cli_machine_data *cli,
             const char *command, void *ud)
{
    (void)ctx; (void)cli; (void)command; (void)ud;
    if (app_driver_trigger_button(BUTTON_B_GPIO) == ESP_OK) { printf("button B queued\n"); return; }
    printf("button B trigger failed\n");
}

static void
cmd_status(rx_fsm_context *ctx, cli_machine_data *cli,
           const char *command, void *ud)
{
    const app_data *app = (const app_data *)ud;
    (void)ctx; (void)cli; (void)command;
    printf(
        "A(light): %s | B(blink): %s output=%d | C(auto): %s\n",
        light_state(app->light_a, 1),
        blink_state(app->blink_b),
        blink_pn_get_output_enabled(app->blink_b),
        light_state(app->auto_c, 1)
    );
    printf(
        "B base_hz=%u | C auto-off=%u ms\n",
        blink_pn_get_base_hz(app->blink_b),
        auto_pn_get_timeout_ms(app->auto_c)
    );
}

static void
cmd_freq(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *ud)
{
    app_data *app = (app_data *)ud;
    unsigned int freq_hz = 0u;
    (void)ctx; (void)cli;
    if (sscanf(command, "freq %u", &freq_hz) != 1) { printf("usage: freq <hz>\n"); return; }
    if (freq_hz == 0u) { printf("frequency must be > 0 hz\n"); return; }
    if (blink_pn_set_base_hz(app->blink_b, freq_hz) != 0) { printf("failed\n"); return; }
    printf("blink B base frequency set to %u hz\n", freq_hz);
}

static void
cmd_timeout(rx_fsm_context *ctx, cli_machine_data *cli,
            const char *command, void *ud)
{
    app_data *app = (app_data *)ud;
    unsigned int timeout_ms = 0u;
    (void)ctx; (void)cli;
    if (sscanf(command, "timeout %u", &timeout_ms) != 1) { printf("usage: timeout <ms>\n"); return; }
    if (timeout_ms == 0u) { printf("timeout must be > 0 ms\n"); return; }
    if (auto_pn_set_timeout_ms(app->auto_c, timeout_ms) != 0) { printf("failed\n"); return; }
    printf("auto C timeout set to %u ms\n", timeout_ms);
}

static void
cmd_help(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *ud)
{
    size_t i;
    (void)ctx; (void)command; (void)ud;
    printf("Commands:\n");
    for (i = 0; i < cli->command_count; ++i)
        printf(" - %s\n", cli->commands[i].name);
    printf(" - freq <hz>\n - timeout <ms>\n");
}

static void
cmd_quit(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *ud)
{
    (void)ctx; (void)cli; (void)command; (void)ud;
    printf("bye\n");
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main(void)
{
    rx_pn_net light_a, blink_b, auto_c;
    rx_pn_runtime pn_rt;
    rx_fsm_runtime cli_rt;
    rx_fsm_machine cli_machine;
    app_data app = {&light_a, &blink_b, &auto_c};
    cli_machine_data cli_data;
    rx_coop_exec ce;
    rx_example_coop_sched_command sched_cmd = {"coop", &ce};

    if (rx_pn_runtime_init(&pn_rt,  3) != 0 ||
        rx_fsm_runtime_init(&cli_rt, 1) != 0) {
        fprintf(stderr, "runtime_init failed\n");
        return 1;
    }

    if (light_pn_init(&light_a, BUTTON_A_GPIO, LIGHT_A_GPIO) != 0 ||
        blink_pn_init(&blink_b, BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ) != 0 ||
        auto_pn_init(&auto_c,   BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS) != 0) {
        fprintf(stderr, "pn_init failed\n");
        return 1;
    }

    if (rx_pn_runtime_add_net(&pn_rt, &light_a, FAST_PERIOD_US, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_rt, &blink_b, FAST_PERIOD_US, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_rt, &auto_c,  SLOW_PERIOD_US, 0) != 0) {
        fprintf(stderr, "add_net failed\n");
        return 1;
    }

    cli_fsm_data_init(&cli_data, &app);
    if (cli_fsm_register_command(&cli_data, "a",        cmd_button_a, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "press a",  cmd_button_a, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "b",        cmd_button_b, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "press b",  cmd_button_b, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "status",   cmd_status,   &app) != 0 ||
        cli_fsm_register_command(&cli_data, "freq",     cmd_freq,     &app) != 0 ||
        cli_fsm_register_command(&cli_data, "timeout",  cmd_timeout,  &app) != 0 ||
        cli_fsm_register_command(&cli_data, "sched",    rx_example_cmd_coop_sched, &sched_cmd) != 0 ||
        cli_fsm_register_command(&cli_data, "help",     cmd_help,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit",     cmd_quit,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit",     cmd_quit,     NULL) != 0) {
        fprintf(stderr, "register_command failed\n");
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&cli_rt, &cli_machine, CLI_PERIOD_US, 0) != 0) {
        fprintf(stderr, "add_machine cli failed\n");
        return 1;
    }

    cmd_help(&cli_rt.context, &cli_data, "help", NULL);
    cmd_status(&cli_rt.context, &cli_data, "status", &app);
    cli_fsm_print_prompt(&cli_data);

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &pn_rt.runtime);   /* light_a + blink_b + auto_c */
    rx_coop_exec_add(&ce, &cli_rt.runtime);  /* cli */
    rx_coop_exec_enable_sched_check(&ce, 1);
    rx_coop_exec_run(&ce); /* never returns */

    return 0;
}
