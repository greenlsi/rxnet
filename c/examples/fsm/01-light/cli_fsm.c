// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "cli_fsm.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

enum {
    CLI_STATE_RUNNING = 0,
};

static struct termios s_saved_termios;
static int s_saved_flags = -1;
static int s_terminal_configured = 0;
static int s_atexit_registered = 0;

static int cli_terminal_enter_raw_mode(void) {
    struct termios raw;
    int flags;

    if (s_terminal_configured) {
        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &s_saved_termios) != 0) {
        return -1;
    }

    raw = s_saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return -1;
    }

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_saved_termios);
        return -1;
    }

    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_saved_termios);
        return -1;
    }

    s_saved_flags = flags;
    s_terminal_configured = 1;
    return 0;
}

static void cli_terminal_leave_raw_mode(void) {
    if (!s_terminal_configured) {
        return;
    }

    if (s_saved_flags >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, s_saved_flags);
    }
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_saved_termios);
    s_saved_flags = -1;
    s_terminal_configured = 0;
}

static void cli_trim(char *text) {
    size_t start = 0;
    size_t end;
    size_t i;

    if (text == NULL) {
        return;
    }

    end = strlen(text);
    while (start < end && isspace((unsigned char)text[start])) {
        start += 1;
    }

    while (end > start && isspace((unsigned char)text[end - 1])) {
        end -= 1;
    }

    if (start == 0) {
        text[end] = '\0';
        return;
    }

    for (i = 0; start + i < end; ++i) {
        text[i] = text[start + i];
    }
    text[i] = '\0';
}

static int has_char(const rx_fsm_context *ctx, void *user) {
    cli_machine_data *data = (cli_machine_data *)user;
    unsigned char ch = 0;
    ssize_t read_result;

    if (data->on_tick != NULL) {
        data->on_tick(ctx, data, data->user_data);
    }

    data->in.char_ready = false;
    data->in.ch = 0;

    read_result = read(STDIN_FILENO, &ch, 1);
    if (read_result == 1) {
        data->in.char_ready = true;
        data->in.ch = ch;
    } else if (read_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("cli_fsm read");
        exit(1);
    }

    return data->in.char_ready;
}

static void run_command(rx_fsm_context *ctx, cli_machine_data *data, const char *command) {
    size_t i;

    for (i = 0; i < data->command_count; ++i) {
        const cli_fsm_command *entry = &data->commands[i];

        if (strcmp(command, entry->name) == 0) {
            entry->handler(ctx, data, command, entry->user_data);
            return;
        }
    }

    if (command[0] != '\0') {
        printf("Unknown command: %s\n", command);
    }
}

static void handle_char(rx_fsm_context *ctx, void *user) {
    cli_machine_data *data = (cli_machine_data *)user;
    unsigned char ch = data->in.ch;

    if (ch == '\r' || ch == '\n') {
        char command[sizeof(data->cmdline)];

        putchar('\n');
        memcpy(command, data->cmdline, data->cmdline_len);
        command[data->cmdline_len] = '\0';
        data->cmdline_len = 0;
        data->cmdline[0] = '\0';

        cli_trim(command);
        run_command(ctx, data, command);
        cli_fsm_print_prompt(data);
        return;
    }

    if (ch == 0x7f || ch == 0x08) {
        if (data->cmdline_len > 0) {
            data->cmdline_len -= 1;
            data->cmdline[data->cmdline_len] = '\0';
            if (data->echo_input) {
                printf("\b \b");
                fflush(stdout);
            }
        }
        return;
    }

    if (isprint(ch) && data->cmdline_len + 1 < sizeof(data->cmdline)) {
        data->cmdline[data->cmdline_len++] = (char)ch;
        data->cmdline[data->cmdline_len] = '\0';
        if (data->echo_input) {
            putchar((int)ch);
            fflush(stdout);
        }
    }
}

static void cli_latch_inputs(rx_fsm_context *ctx, void *user) {
}

static void cli_dump_outputs(rx_fsm_context *ctx, void *user) {
}

void cli_fsm_data_init(cli_machine_data *data, void *user_data) {
    if (data == NULL) {
        return;
    }

    memset(data, 0, sizeof(*data));
    data->user_data = user_data;
    data->echo_input = true;
    data->print_prompt = true;
}

int cli_fsm_register_command(
    cli_machine_data *data,
    const char *name,
    cli_fsm_command_fn handler,
    void *command_user_data
) {
    cli_fsm_command *entry;

    if (data == NULL || name == NULL || name[0] == '\0' || handler == NULL) {
        return -1;
    }

    if (data->command_count >= CLI_FSM_MAX_COMMANDS) {
        return -1;
    }

    entry = &data->commands[data->command_count++];
    entry->name = name;
    entry->handler = handler;
    entry->user_data = command_user_data;
    return 0;
}

void cli_fsm_print_prompt(const cli_machine_data *data) {
    if (data == NULL || !data->print_prompt) {
        return;
    }

    printf("> ");
    fflush(stdout);
}

void cli_fsm_create(rx_fsm_machine *machine, const char *name, cli_machine_data *data) {
    static const rx_fsm_transition transitions[] = {
        {CLI_STATE_RUNNING, CLI_STATE_RUNNING, has_char, handle_char},
    };

    rx_fsm_machine_init(
        machine,
        name,
        CLI_STATE_RUNNING,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data,
        cli_latch_inputs,
        cli_dump_outputs
    );

    if (!s_atexit_registered) {
        (void)atexit(cli_terminal_leave_raw_mode);
        s_atexit_registered = 1;
    }

    if (cli_terminal_enter_raw_mode() != 0) {
        fprintf(stderr, "cli_terminal_enter_raw_mode failed\n");
        exit(1);
    }
    data->in_raw_mode = true;

    cli_fsm_print_prompt(data);
}
