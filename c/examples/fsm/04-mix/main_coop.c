// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * FSM 03-mix — cooperative (single-thread) multi-rate scheduler.
 *
 * Each machine carries its own period; one runtime holds them all.
 * rx_coop_exec runs whichever runtime is due, then sleeps until the
 * nearest upcoming deadline — no preemption, no OS threads.
 *
 *   light_a  10 ms   (button A → light A)
 *   blink_b  10 ms   (button A → blink B)
 *   auto_c   20 ms   (button B → auto-off light C)
 *   cli      10 ms   (stdin command loop)
 *
 *   runtime base tick = GCD(10, 10, 20, 10) = 10 ms
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
 *   make -C c mix_coop
 *   ./c/build/fsm_03_mix_coop
 */

#include <stdio.h>
#include <stdlib.h>

#include "rxnet/coop.h"
#include "rxnet/fsm.h"

#include "app_driver.h"
#include "auto_fsm.h"
#include "blink_fsm.h"
#include "cli_fsm.h"
#include "light_fsm.h"
#include "sched_report.h"

#define LIGHT_A_GPIO         2
#define LIGHT_B_GPIO         4
#define LIGHT_C_GPIO         5
#define BUTTON_A_GPIO        0
#define BUTTON_B_GPIO        15
#define FAST_PERIOD_US       10000L   /* 10 ms */
#define SLOW_PERIOD_US       20000L   /* 20 ms */
#define CLI_PERIOD_US        10000L   /* 10 ms */
#define DEFAULT_FREQ_B_HZ    2u
#define DEFAULT_TIMEOUT_C_MS 9000u

typedef struct {
    rx_fsm_machine *light_a_machine;
    rx_fsm_machine *blink_b_machine;
    rx_fsm_machine *auto_c_machine;
} mix_app_data;

/* ------------------------------------------------------------------ */
/* CLI commands                                                         */
/* ------------------------------------------------------------------ */

static const char *light_state_name(int state) {
    return state == 0 ? "OFF" : "ON";
}

static const char *blink_state_name(int state) {
    switch (state) {
        case 1: return "BLINK_X1";
        case 2: return "BLINK_X2";
        default: return "OFF";
    }
}

static unsigned int blink_effective_hz(const rx_fsm_machine *m) {
    unsigned int base = blink_fsm_get_base_hz(m);
    if (m->state == 2) return base * 2u;
    if (m->state == 1) return base;
    return 0u;
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
    const mix_app_data *app = (const mix_app_data *)ud;
    (void)ctx; (void)cli; (void)command;
    printf(
        "A(light): state=%s output=%d | B(blink): state=%s output=%d | C(auto): state=%s output=%d\n",
        light_state_name(app->light_a_machine->state),
        app->light_a_machine->state != 0,
        blink_state_name(app->blink_b_machine->state),
        blink_fsm_get_output_enabled(app->blink_b_machine),
        light_state_name(app->auto_c_machine->state),
        app->auto_c_machine->state != 0
    );
    printf(
        "B freq(hz): base=%u effective=%u | C auto-off(ms)=%u\n",
        blink_fsm_get_base_hz(app->blink_b_machine),
        blink_effective_hz(app->blink_b_machine),
        auto_fsm_get_auto_off_timeout_ms(app->auto_c_machine)
    );
}

static void
cmd_freq(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *ud)
{
    mix_app_data *app = (mix_app_data *)ud;
    unsigned int freq_hz = 0u;
    (void)ctx; (void)cli;
    if (sscanf(command, "freq %u", &freq_hz) != 1) { printf("usage: freq <hz>\n"); return; }
    if (freq_hz == 0u) { printf("frequency must be > 0 hz\n"); return; }
    if (blink_fsm_set_base_hz(app->blink_b_machine, freq_hz) != 0) { printf("failed\n"); return; }
    printf("blink B base frequency set to %u hz\n", freq_hz);
}

static void
cmd_timeout(rx_fsm_context *ctx, cli_machine_data *cli,
            const char *command, void *ud)
{
    mix_app_data *app = (mix_app_data *)ud;
    unsigned int timeout_ms = 0u;
    (void)ctx; (void)cli;
    if (sscanf(command, "timeout %u", &timeout_ms) != 1) { printf("usage: timeout <ms>\n"); return; }
    if (timeout_ms == 0u) { printf("timeout must be > 0 ms\n"); return; }
    if (auto_fsm_set_auto_off_timeout_ms(app->auto_c_machine, timeout_ms) != 0) { printf("failed\n"); return; }
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
    rx_fsm_machine light_a_machine, blink_b_machine, auto_c_machine, cli_machine;
    rx_fsm_runtime runtime;
    mix_app_data app = {&light_a_machine, &blink_b_machine, &auto_c_machine};
    cli_machine_data cli_data;
    rx_coop_exec ce;
    rx_example_coop_sched_command sched_cmd = {"coop", &ce};

    if (rx_fsm_runtime_init(&runtime, 4) != 0) {
        fprintf(stderr, "runtime_init failed\n");
        return 1;
    }

    light_fsm_create(&light_a_machine, BUTTON_A_GPIO, LIGHT_A_GPIO);
    blink_fsm_create(&blink_b_machine, BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ);
    auto_fsm_create(&auto_c_machine,   BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS);

    if (rx_fsm_runtime_add_machine(&runtime, &light_a_machine, FAST_PERIOD_US, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &blink_b_machine, FAST_PERIOD_US, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &auto_c_machine,  SLOW_PERIOD_US, 0) != 0) {
        fprintf(stderr, "add_machine failed\n");
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

    if (rx_fsm_runtime_add_machine(&runtime, &cli_machine, CLI_PERIOD_US, 0) != 0) {
        fprintf(stderr, "add_machine cli failed\n");
        return 1;
    }

    cmd_help(&runtime.context, &cli_data, "help", NULL);
    cmd_status(&runtime.context, &cli_data, "status", &app);
    cli_fsm_print_prompt(&cli_data);

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &runtime.runtime);
    rx_coop_exec_enable_sched_check(&ce, 1);
    rx_coop_exec_run(&ce); /* never returns */

    return 0;
}
