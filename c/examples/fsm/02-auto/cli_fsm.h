// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "rxnet/fsm.h"

#define CLI_FSM_MAX_COMMANDS 16
#define CLI_FSM_MAX_CMDLINE 64

typedef struct cli_machine_data cli_machine_data;

typedef void (*cli_fsm_command_fn)(
    rx_fsm_context *ctx,
    cli_machine_data *cli,
    const char *command,
    void *command_user_data
);

typedef void (*cli_fsm_tick_fn)(const rx_fsm_context *ctx, cli_machine_data *cli, void *user_data);

typedef struct {
    const char *name;
    cli_fsm_command_fn handler;
    void *user_data;
} cli_fsm_command;

struct cli_machine_data {
    void *user_data;
    cli_fsm_tick_fn on_tick;
    bool echo_input;
    bool print_prompt;
    bool in_raw_mode;
    char cmdline[CLI_FSM_MAX_CMDLINE];
    size_t cmdline_len;
    struct {
        bool char_ready;
        unsigned char ch;
    } in;
    cli_fsm_command commands[CLI_FSM_MAX_COMMANDS];
    size_t command_count;
};

void cli_fsm_data_init(cli_machine_data *data, void *user_data);
int cli_fsm_register_command(
    cli_machine_data *data,
    const char *name,
    cli_fsm_command_fn handler,
    void *command_user_data
);
void cli_fsm_print_prompt(const cli_machine_data *data);

void cli_fsm_create(rx_fsm_machine *machine, const char *name, cli_machine_data *data);
