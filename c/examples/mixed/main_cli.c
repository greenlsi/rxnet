// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

/*
 * examples/mixed/main_cli.c
 *
 * Demonstrates that FSM machines and PN nets are both rx_node subtypes
 * and can share a single base rx_runtime tick.
 *
 * Light A (GPIO 2): toggled by button A — implemented as an FSM.
 * Light B (GPIO 4): toggled by button B — implemented as a PN net.
 *
 * Both light_a and light_b are registered in one rx_runtime.  A single
 * rx_tick() call advances them both — regardless of model family.
 *
 * The CLI lives in its own rx_fsm_runtime (separate tick) so the
 * mixed-runtime concept is clearly visible in main().
 */

#include <stdio.h>
#include <stdlib.h>

#include "rxnet/coop.h"
#include "rxnet/runtime.h"   /* rx_context, rx_runtime, rx_tick */
#include "rxnet/fsm.h"
#include "rxnet/pn.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "light_fsm.h"
#include "light_pn.h"

#define BUTTON_A_GPIO  0
#define BUTTON_B_GPIO  15
#define LIGHT_A_GPIO   2
#define LIGHT_B_GPIO   4
#define PERIOD_US      (10 * 1000)

/* ------------------------------------------------------------------ */
/* CLI commands                                                         */
/* ------------------------------------------------------------------ */

static void cmd_a(rx_fsm_context *ctx, cli_machine_data *cli,
                  const char *command, void *user)
{
    (void)ctx; (void)cli; (void)command; (void)user;
    if (app_driver_trigger_button(BUTTON_A_GPIO) == ESP_OK) {
        printf("button A queued (FSM light)\n");
        return;
    }
    printf("button A trigger failed\n");
}

static void cmd_b(rx_fsm_context *ctx, cli_machine_data *cli,
                  const char *command, void *user)
{
    (void)ctx; (void)cli; (void)command; (void)user;
    if (app_driver_trigger_button(BUTTON_B_GPIO) == ESP_OK) {
        printf("button B queued (PN light)\n");
        return;
    }
    printf("button B trigger failed\n");
}

static void cmd_quit(rx_fsm_context *ctx, cli_machine_data *cli,
                     const char *command, void *user)
{
    (void)ctx; (void)cli; (void)command; (void)user;
    printf("bye\n");
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    /*
     * Base runtime: one rx_context + one rx_runtime shared by
     * an FSM machine (light_a) and a PN net (light_b).
     */
    rx_context     ctx;
    rx_runtime     rt;

    rx_fsm_machine light_a;   /* FSM node: button A toggles light A */
    rx_pn_net      light_b;   /* PN node : button B toggles light B */

    /* CLI lives in its own rx_fsm_runtime (separate concern). */
    rx_fsm_runtime cli_rt;
    rx_fsm_machine cli_machine;
    cli_machine_data cli_data;
    rx_coop_exec ce;

    /* --- initialise base runtime ----------------------------------- */
    if (rx_context_init(&ctx) != 0) {
        fprintf(stderr, "rx_context_init failed\n");
        return 1;
    }
    if (rx_runtime_init(&rt, &ctx, 2) != 0) {
        fprintf(stderr, "rx_runtime_init failed\n");
        rx_context_free(&ctx);
        return 1;
    }

    /* --- initialise app nodes -------------------------------------- */
    light_fsm_create(&light_a, BUTTON_A_GPIO, LIGHT_A_GPIO);
    if (light_pn_init(&light_b, BUTTON_B_GPIO, LIGHT_B_GPIO) != 0) {
        fprintf(stderr, "light_pn_init failed\n");
        rx_context_free(&ctx);
        return 1;
    }

    /* Register both in the SAME base runtime. */
    if (rx_runtime_add_node(&rt, &light_a.node, PERIOD_US, 0) != 0 ||
        rx_runtime_add_node(&rt, &light_b.node, PERIOD_US, 0) != 0) {
        fprintf(stderr, "rx_runtime_add_node failed\n");
        rx_pn_net_free(&light_b);
        rx_context_free(&ctx);
        return 1;
    }

    /* --- initialise CLI -------------------------------------------- */
    if (rx_fsm_runtime_init(&cli_rt, 1) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        rx_pn_net_free(&light_b);
        rx_context_free(&ctx);
        return 1;
    }

    cli_fsm_data_init(&cli_data, NULL);
    if (cli_fsm_register_command(&cli_data, "a",    cmd_a,    NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "b",    cmd_b,    NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit", cmd_quit, NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit", cmd_quit, NULL) != 0) {
        fprintf(stderr, "cli_fsm_register_command failed\n");
        rx_pn_net_free(&light_b);
        rx_fsm_runtime_free(&cli_rt);
        rx_context_free(&ctx);
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&cli_rt, &cli_machine, PERIOD_US, 0) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_pn_net_free(&light_b);
        rx_fsm_runtime_free(&cli_rt);
        rx_context_free(&ctx);
        return 1;
    }

    printf("Commands: a (FSM light), b (PN light), quit\n");
    cli_fsm_print_prompt(&cli_data);

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &cli_rt.runtime);
    rx_coop_exec_add(&ce, &rt);
    rx_coop_exec_run(&ce); /* never returns */

    return 0;
}
