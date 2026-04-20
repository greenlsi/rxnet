// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

/*
 * PN 04-mix — parallel thread-per-node scheduler + tracing.
 *
 * Identical to main_threads.c but with the rxnet tracing subsystem enabled.
 * The tracer mutex is thread-safe: the three PN node threads and the CLI
 * thread write concurrently without data races.
 *
 * Extra CLI command:
 *   trace          — export trace.bin and print visualisation instructions
 *
 * Build:
 *   make -C c pn_04_mix_threads_trace
 *   ./c/build/pn_04_mix_threads_trace
 *   > trace
 *   python -m rxnet.tools.trace trace.bin --report trace.html --open
 */

#include <stdio.h>
#include <stdlib.h>

#ifndef RX_TRACE_ENABLE
#define RX_TRACE_ENABLE
#endif
#include "rxnet/fsm.h"
#include "rxnet/pn.h"
#include "rxnet/thread.h"
#include "rxnet/trace.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "light_pn.h"
#include "auto_pn.h"
#include "blink_pn.h"

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

/* light_pn / auto_pn place ids */
enum { P_OFF = 0, P_ON = 1, P_REQUEST = 2 };

typedef struct {
    rx_pn_net *light_a;
    rx_pn_net *blink_b;
    rx_pn_net *auto_c;
} app_data;

/* ── tracer (global for CLI command access) ── */
static rx_trace_buf_t g_tracer;

/* ── CLI commands ─────────────────────────────────────────────────────── */

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
        "A(light): %s | B(blink): %s | C(auto): %s\n",
        light_state(app->light_a, 1),
        blink_state(app->blink_b),
        light_state(app->auto_c, 1)
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
cmd_trace(rx_fsm_context *ctx, cli_machine_data *cli,
          const char *command, void *ud)
{
    (void)ctx; (void)cli; (void)command; (void)ud;
    if (rx_trace_export(&g_tracer, "trace.bin") != 0) {
        printf("error: could not write trace.bin\n");
        return;
    }
    printf("trace written to trace.bin (%u events, %u dropped)\n",
           (unsigned)g_tracer.n, (unsigned)g_tracer.dropped);
    printf("visualise:\n");
    printf("  python -m rxnet.tools.trace trace.bin --report trace.html --open\n");
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

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    rx_pn_net light_a, blink_b, auto_c;
    rx_pn_runtime pn_rt;
    rx_fsm_runtime cli_rt;
    rx_fsm_machine cli_machine;
    app_data app = {&light_a, &blink_b, &auto_c};
    cli_machine_data cli_data;
    rx_thread_exec te;

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
        cli_fsm_register_command(&cli_data, "trace",    cmd_trace,    NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "help",     cmd_help,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit",     cmd_quit,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit",     cmd_quit,     NULL) != 0) {
        fprintf(stderr, "register_command failed\n");
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    /* cli added last → its runtime is last → cli node runs in main thread */
    if (rx_fsm_runtime_add_machine(&cli_rt, &cli_machine, CLI_PERIOD_US, 0) != 0) {
        fprintf(stderr, "add_machine cli failed\n");
        return 1;
    }

    /* ── set up tracer ── */
    rx_trace_init(&g_tracer, 0);

    /* pn_rt: light_a=nid0, blink_b=nid1, auto_c=nid2 */
    rx_trace_attach(&g_tracer, &light_a.node, 0);
    rx_trace_set_node_name(&g_tracer, 0, "light_a");
    rx_trace_set_place_name(&g_tracer, 0, P_OFF,     "OFF");
    rx_trace_set_place_name(&g_tracer, 0, P_ON,      "ON");
    rx_trace_set_place_name(&g_tracer, 0, P_REQUEST, "REQ");
    rx_trace_set_trans_name(&g_tracer, 0, 0, "turn_on");
    rx_trace_set_trans_name(&g_tracer, 0, 1, "turn_off");

    rx_trace_attach(&g_tracer, &blink_b.node, 1);
    rx_trace_set_node_name(&g_tracer, 1, "blink_b");
    rx_trace_set_place_name(&g_tracer, 1, 0, "OFF");
    rx_trace_set_place_name(&g_tracer, 1, 1, "X1");
    rx_trace_set_place_name(&g_tracer, 1, 2, "X2");

    rx_trace_attach(&g_tracer, &auto_c.node, 2);
    rx_trace_set_node_name(&g_tracer, 2, "auto_c");
    rx_trace_set_place_name(&g_tracer, 2, P_OFF,     "OFF");
    rx_trace_set_place_name(&g_tracer, 2, P_ON,      "ON");
    rx_trace_set_place_name(&g_tracer, 2, P_REQUEST, "REQ");

    /* cli_rt: cli=nid3 (separate runtime, same tracer) */
    rx_trace_attach(&g_tracer, &cli_machine.node, 3);
    rx_trace_set_node_name(&g_tracer, 3, "cli");

    cmd_help(&cli_rt.context, &cli_data, "help", NULL);
    cmd_status(&cli_rt.context, &cli_data, "status", &app);
    cli_fsm_print_prompt(&cli_data);

    rx_thread_exec_init(&te);
    rx_thread_exec_add(&te, &pn_rt.runtime);   /* light_a + blink_b + auto_c → threads */
    rx_thread_exec_add(&te, &cli_rt.runtime);  /* cli → main thread (last)             */
    rx_thread_exec_run(&te); /* never returns */

    return 0;
}
