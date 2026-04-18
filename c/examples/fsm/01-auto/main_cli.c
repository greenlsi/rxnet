#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rxnet/fsm.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "auto_fsm.h"

#define LIGHT_A_GPIO 2
#define LIGHT_B_GPIO 4
#define LIGHT_C_GPIO 5
#define BUTTON_A_GPIO 0
#define BUTTON_B_GPIO 15
#define PERIOD_US (10 * 1000)
#define DEFAULT_TIMEOUT_A_MS 3000u
#define DEFAULT_TIMEOUT_B_MS 6000u
#define DEFAULT_TIMEOUT_C_MS 9000u

typedef struct {
    rx_fsm_machine *auto_a_machine;
    rx_fsm_machine *auto_b_machine;
    rx_fsm_machine *auto_c_machine;
} auto_cli_app_data;

static const char *state_name(int state) {
    return state == 0 ? "OFF" : "ON";
}

static unsigned int timeout_ms_for_machine(const rx_fsm_machine *machine) {
    return auto_fsm_get_auto_off_timeout_ms(machine);
}

static rx_fsm_machine *machine_for_id(auto_cli_app_data *app, char id) {
    switch (id) {
        case 'a':
        case 'A':
            return app->auto_a_machine;
        case 'b':
        case 'B':
            return app->auto_b_machine;
        case 'c':
        case 'C':
            return app->auto_c_machine;
        default:
            return NULL;
    }
}

static void
cmd_button_a(rx_fsm_context *ctx, cli_machine_data *cli,
             const char *command, void *command_user_data)
{
    (void)ctx;
    (void)cli;
    (void)command;
    (void)command_user_data;
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
    (void)ctx;
    (void)cli;
    (void)command;
    (void)command_user_data;
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
    const auto_cli_app_data *app = (const auto_cli_app_data *)command_user_data;

    (void)ctx;
    (void)cli;
    (void)command;

    printf(
        "states: A=%s B=%s C=%s | outputs: A=%d B=%d C=%d\n",
        state_name(app->auto_a_machine->state),
        state_name(app->auto_b_machine->state),
        state_name(app->auto_c_machine->state),
        app->auto_a_machine->state != 0,
        app->auto_b_machine->state != 0,
        app->auto_c_machine->state != 0
    );
    printf(
        "auto-off(ms): A=%u B=%u C=%u\n",
        timeout_ms_for_machine(app->auto_a_machine),
        timeout_ms_for_machine(app->auto_b_machine),
        timeout_ms_for_machine(app->auto_c_machine)
    );
}

static void
cmd_timeout(rx_fsm_context *ctx, cli_machine_data *cli,
            const char *command, void *command_user_data)
{
    auto_cli_app_data *app = (auto_cli_app_data *)command_user_data;
    char auto_id = '\0';
    unsigned int timeout_ms = 0u;
    char extra = '\0';
    rx_fsm_machine *machine;

    (void)ctx;
    (void)cli;

    if (sscanf(command, "timeout %c %u %c", &auto_id, &timeout_ms, &extra) != 2) {
        printf("usage: timeout <a|b|c> <ms>\n");
        return;
    }

    machine = machine_for_id(app, auto_id);
    if (machine == NULL) {
        printf("invalid light '%c' (use a, b or c)\n", auto_id);
        return;
    }

    if (timeout_ms == 0u) {
        printf("timeout must be > 0 ms\n");
        return;
    }

    if (auto_fsm_set_auto_off_timeout_ms(machine, timeout_ms) != 0) {
        printf("failed to update timeout for light %c\n", auto_id);
        return;
    }

    printf(
        "light %c auto-off set to %u ms\n",
        auto_id,
        timeout_ms
    );
}

static void
cmd_help(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *command_user_data)
{
    size_t i;

    (void)ctx;
    (void)command;
    (void)command_user_data;

    printf("Commands:\n");
    for (i = 0; i < cli->command_count; ++i) {
        printf(" - %s\n", cli->commands[i].name);
    }
    printf(" - timeout <a|b|c> <ms>\n");
}

static void
cmd_quit(rx_fsm_context *ctx, cli_machine_data *cli,
         const char *command, void *command_user_data)
{
    (void)ctx;
    (void)cli;
    (void)command;
    (void)command_user_data;
    printf("bye\n");
    exit(0);
}

static struct timespec
timespec_add_us(struct timespec t, long us)
{
    t.tv_nsec += us * 1000L;
    while (t.tv_nsec >= 1000000000L) {
        t.tv_nsec -= 1000000000L;
        ++t.tv_sec;
    }
    return t;
}

static int
timespec_compare(struct timespec a, struct timespec b)
{
    if (a.tv_sec < b.tv_sec) {
        return -1;
    }
    if (a.tv_sec > b.tv_sec) {
        return 1;
    }
    if (a.tv_nsec < b.tv_nsec) {
        return -1;
    }
    if (a.tv_nsec > b.tv_nsec) {
        return 1;
    }
    return 0;
}

int
main(void)
{
    rx_fsm_runtime runtime;
    rx_fsm_machine auto_a_machine;
    rx_fsm_machine auto_b_machine;
    rx_fsm_machine auto_c_machine;
    rx_fsm_machine cli_machine;

    auto_cli_app_data app_data = {
        .auto_a_machine = &auto_a_machine,
        .auto_b_machine = &auto_b_machine,
        .auto_c_machine = &auto_c_machine,
    };
    cli_machine_data cli_data;
    struct timespec now;
    struct timespec next_tick;

    if (rx_fsm_runtime_init(&runtime, 4) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        return 1;
    }

    auto_fsm_create(&auto_a_machine, BUTTON_A_GPIO, LIGHT_A_GPIO, DEFAULT_TIMEOUT_A_MS);
    auto_fsm_create(&auto_b_machine, BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_TIMEOUT_B_MS);
    auto_fsm_create(&auto_c_machine, BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS);

    cli_fsm_data_init(&cli_data, &app_data);
    if (cli_fsm_register_command(&cli_data, "a", cmd_button_a, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "press a", cmd_button_a, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "b", cmd_button_b, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "press b", cmd_button_b, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "status", cmd_status, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "timeout", cmd_timeout, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "help", cmd_help, NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit", cmd_quit, NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit", cmd_quit, NULL) != 0) {
        fprintf(stderr, "cli_fsm_register_command failed\n");
        rx_fsm_runtime_free(&runtime);
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&runtime, &auto_a_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &auto_b_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &auto_c_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &cli_machine, 0, 0) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_fsm_runtime_free(&runtime);
        return 1;
    }

    cmd_help(&runtime.context, &cli_data, "help", NULL);
    cmd_status(&runtime.context, &cli_data, "status", &app_data);
    cli_fsm_print_prompt(&cli_data);

    if (clock_gettime(CLOCK_MONOTONIC, &next_tick) != 0) {
        fprintf(stderr, "clock_gettime failed\n");
        rx_fsm_runtime_free(&runtime);
        return 1;
    }
    next_tick = timespec_add_us(next_tick, PERIOD_US);

    while (1) {
        if (rx_fsm_tick(&runtime) != 0) {
            fprintf(stderr, "rx_fsm_tick failed\n");
            break;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            fprintf(stderr, "clock_gettime failed\n");
            break;
        }

        while (timespec_compare(now, next_tick) >= 0) {
            next_tick = timespec_add_us(next_tick, PERIOD_US);
        }

        while (1) {
            struct timespec sleep_time = {
                .tv_sec = next_tick.tv_sec - now.tv_sec,
                .tv_nsec = next_tick.tv_nsec - now.tv_nsec,
            };

            if (sleep_time.tv_nsec < 0) {
                sleep_time.tv_nsec += 1000000000L;
                --sleep_time.tv_sec;
            }

            if (sleep_time.tv_sec < 0) {
                break;
            }

            if (nanosleep(&sleep_time, NULL) == 0) {
                break;
            }

            if (errno != EINTR) {
                fprintf(stderr, "nanosleep failed\n");
                break;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                fprintf(stderr, "clock_gettime failed\n");
                break;
            }
        }

        next_tick = timespec_add_us(next_tick, PERIOD_US);
    }

    rx_fsm_runtime_free(&runtime);
    return 0;
}
