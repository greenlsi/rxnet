#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rxnet/fsm.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "light_fsm.h"

#define LIGHT_A_GPIO 2
#define LIGHT_B_GPIO 4
#define LIGHT_C_GPIO 5
#define BUTTON_A_GPIO 0
#define BUTTON_B_GPIO 15

typedef struct {
    const rx_fsm_machine *light_a_machine;
    const rx_fsm_machine *light_b_machine;
    const rx_fsm_machine *light_c_machine;
} light_cli_app_data;

static const char *state_name(int state) {
    return state == 0 ? "OFF" : "ON";
}

static void 
cmd_button_a(rx_fsm_context *ctx, cli_machine_data *cli, 
             const char *command, void *command_user_data) 
{
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
    const light_cli_app_data *app = (const light_cli_app_data *)command_user_data;

    printf(
        "states: A=%s B=%s C=%s | outputs: A=%d B=%d C=%d\n",
        state_name(app->light_a_machine->state),
        state_name(app->light_b_machine->state),
        state_name(app->light_c_machine->state),
        app->light_a_machine->state != 0,
        app->light_b_machine->state != 0,
        app->light_c_machine->state != 0
    );
}

static void 
cmd_help(rx_fsm_context *ctx, cli_machine_data *cli, 
         const char *command, void *command_user_data) 
{
    size_t i;

    printf("Commands:\n");
    for (i = 0; i < cli->command_count; ++i) {
        printf(" - %s\n", cli->commands[i].name);
    }
}

static void 
cmd_quit(rx_fsm_context *ctx, cli_machine_data *cli, 
         const char *command, void *command_user_data) 
{
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
    rx_fsm_machine light_a_machine;
    rx_fsm_machine light_b_machine;
    rx_fsm_machine light_c_machine;
    rx_fsm_machine cli_machine;

    light_cli_app_data app_data = {
        .light_a_machine = &light_a_machine,
        .light_b_machine = &light_b_machine,
        .light_c_machine = &light_c_machine,
    };
    cli_machine_data cli_data;
    const long period_us = 10 * 1000;
    struct timespec now;
    struct timespec next_tick;

    if (rx_fsm_runtime_init(&runtime, 4) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        return 1;
    }

    light_fsm_create(&light_a_machine, BUTTON_A_GPIO, LIGHT_A_GPIO);
    light_fsm_create(&light_b_machine, BUTTON_A_GPIO, LIGHT_B_GPIO);
    light_fsm_create(&light_c_machine, BUTTON_B_GPIO, LIGHT_C_GPIO);

    cli_fsm_data_init(&cli_data, &app_data);
    if (cli_fsm_register_command(&cli_data, "a", cmd_button_a, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "press a", cmd_button_a, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "b", cmd_button_b, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "press b", cmd_button_b, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "status", cmd_status, &app_data) != 0 ||
        cli_fsm_register_command(&cli_data, "help", cmd_help, NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit", cmd_quit, NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit", cmd_quit, NULL) != 0) {
        fprintf(stderr, "cli_fsm_register_command failed\n");
        rx_fsm_runtime_free(&runtime);
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&runtime, &light_a_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_b_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_c_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &cli_machine) != 0) {
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
    next_tick = timespec_add_us(next_tick, period_us);

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
            next_tick = timespec_add_us(next_tick, period_us);
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

        next_tick = timespec_add_us(next_tick, period_us);
    }

    rx_fsm_runtime_free(&runtime);
    return 0;
}
