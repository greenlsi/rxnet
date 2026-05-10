// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>

#include "rxnet/coop.h"
#include "rxnet/fsm.h"
#include "rxnet/pn.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "blink_pn.h"
#include "sched_report.h"

#define LIGHT_A_GPIO     2
#define LIGHT_B_GPIO     4
#define LIGHT_C_GPIO     5
#define BUTTON_A_GPIO    0
#define BUTTON_B_GPIO    15
#define PERIOD_US        (10 * 1000)
#define DEFAULT_HZ_A     1u
#define DEFAULT_HZ_B     2u
#define DEFAULT_HZ_C     4u

typedef struct {
    rx_pn_net *blink_a;
    rx_pn_net *blink_b;
    rx_pn_net *blink_c;
} app_data;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static const char *blink_state(const rx_pn_net *net) {
    if (net == NULL) return "OFF";
    if (net->places[2] > 0) return "X2";   /* P_X2 = 2 */
    if (net->places[1] > 0) return "X1";   /* P_X1 = 1 */
    return "OFF";
}

static rx_pn_net *net_for_id(app_data *app, char id) {
    switch (id) {
        case 'a': case 'A': return app->blink_a;
        case 'b': case 'B': return app->blink_b;
        case 'c': case 'C': return app->blink_c;
        default:            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* CLI commands                                                         */
/* ------------------------------------------------------------------ */

static void
cmd_button_a(rx_fsm_context *ctx, cli_machine_data *cli,
             const char *command, void *command_user_data)
{
    (void)ctx; (void)cli; (void)command; (void)command_user_data;
    if (app_driver_trigger_button(BUTTON_A_GPIO) == ESP_OK) {
        printf("button A queued\n");
        return;
    }
    printf("button A trigger failed\n");
}

static void
cmd_button_b(rx_fsm_context *ctx, cli_machine_data *cli,
             const char *command, void *command_user_data)
{
    (void)ctx; (void)cli; (void)command; (void)command_user_data;
    if (app_driver_trigger_button(BUTTON_B_GPIO) == ESP_OK) {
        printf("button B queued\n");
        return;
    }
    printf("button B trigger failed\n");
}

static void
cmd_status(rx_fsm_context *ctx, cli_machine_data *cli,
           const char *command, void *command_user_data)
{
    const app_data *app = (const app_data *)command_user_data;

    (void)ctx; (void)cli; (void)command;
    printf(
        "A: %s (output=%d)  B: %s (output=%d)  C: %s (output=%d)\n",
        blink_state(app->blink_a), blink_pn_get_output_enabled(app->blink_a),
        blink_state(app->blink_b), blink_pn_get_output_enabled(app->blink_b),
        blink_state(app->blink_c), blink_pn_get_output_enabled(app->blink_c)
    );
    printf(
        "base_hz: A=%u  B=%u  C=%u\n",
        blink_pn_get_base_hz(app->blink_a),
        blink_pn_get_base_hz(app->blink_b),
        blink_pn_get_base_hz(app->blink_c)
    );
}

static void
cmd_freq(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *command_user_data)
{
    app_data *app = (app_data *)command_user_data;
    char blink_id = '\0';
    unsigned int freq_hz = 0u;
    char extra = '\0';
    rx_pn_net *net;

    (void)ctx; (void)cli;

    if (sscanf(command, "freq %c %u %c", &blink_id, &freq_hz, &extra) != 2) {
        printf("usage: freq <a|b|c> <hz>\n");
        return;
    }

    net = net_for_id(app, blink_id);
    if (net == NULL) {
        printf("invalid light '%c' (use a, b or c)\n", blink_id);
        return;
    }

    if (freq_hz == 0u) {
        printf("frequency must be > 0 hz\n");
        return;
    }

    if (blink_pn_set_base_hz(net, freq_hz) != 0) {
        printf("failed to update frequency for light %c\n", blink_id);
        return;
    }

    printf("light %c base frequency set to %u hz\n", blink_id, freq_hz);
}

static void
cmd_help(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *command_user_data)
{
    size_t i;

    (void)ctx; (void)command; (void)command_user_data;
    printf("Commands:\n");
    for (i = 0; i < cli->command_count; ++i) {
        printf(" - %s\n", cli->commands[i].name);
    }
    printf(" - freq <a|b|c> <hz>\n");
}

static void
cmd_quit(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *command_user_data)
{
    (void)ctx; (void)cli; (void)command; (void)command_user_data;
    printf("bye\n");
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main(void)
{
    rx_pn_runtime pn_runtime;
    rx_fsm_runtime fsm_runtime;

    rx_pn_net blink_a, blink_b, blink_c;
    rx_fsm_machine cli_machine;

    app_data app = {&blink_a, &blink_b, &blink_c};
    cli_machine_data cli_data;
    rx_coop_exec ce;
    rx_example_coop_sched_command sched_cmd = {"coop", &ce};

    if (rx_pn_runtime_init(&pn_runtime, 3) != 0) {
        fprintf(stderr, "rx_pn_runtime_init failed\n");
        return 1;
    }
    if (rx_fsm_runtime_init(&fsm_runtime, 1) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        rx_pn_runtime_free(&pn_runtime);
        return 1;
    }

    if (blink_pn_init(&blink_a, BUTTON_A_GPIO, LIGHT_A_GPIO, DEFAULT_HZ_A) != 0 ||
        blink_pn_init(&blink_b, BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_HZ_B) != 0 ||
        blink_pn_init(&blink_c, BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_HZ_C) != 0) {
        fprintf(stderr, "blink_pn_init failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }

    if (rx_pn_runtime_add_net(&pn_runtime, &blink_a, PERIOD_US, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_runtime, &blink_b, PERIOD_US, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_runtime, &blink_c, PERIOD_US, 0) != 0) {
        fprintf(stderr, "rx_pn_runtime_add_net failed\n");
        rx_pn_net_free(&blink_a);
        rx_pn_net_free(&blink_b);
        rx_pn_net_free(&blink_c);
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }

    cli_fsm_data_init(&cli_data, &app);
    if (cli_fsm_register_command(&cli_data, "a",       cmd_button_a, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "press a", cmd_button_a, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "b",       cmd_button_b, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "press b", cmd_button_b, &app) != 0 ||
        cli_fsm_register_command(&cli_data, "status",  cmd_status,   &app) != 0 ||
        cli_fsm_register_command(&cli_data, "freq",    cmd_freq,     &app) != 0 ||
        cli_fsm_register_command(&cli_data, "sched",   rx_example_cmd_coop_sched, &sched_cmd) != 0 ||
        cli_fsm_register_command(&cli_data, "help",    cmd_help,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit",    cmd_quit,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit",    cmd_quit,     NULL) != 0) {
        fprintf(stderr, "cli_fsm_register_command failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&fsm_runtime, &cli_machine, PERIOD_US, 0) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }

    cmd_help(&fsm_runtime.context, &cli_data, "help", NULL);
    cmd_status(&fsm_runtime.context, &cli_data, "status", &app);
    cli_fsm_print_prompt(&cli_data);

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &fsm_runtime.runtime);
    rx_coop_exec_add(&ce, &pn_runtime.runtime);
    rx_coop_exec_enable_sched_check(&ce, 1);
    rx_coop_exec_run(&ce); /* never returns */

    return 0;
}
