#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rxnet/fsm.h"
#include "rxnet/pn.h"

#include "app_driver.h"
#include "cli_fsm.h"
#include "auto_pn.h"

#define LIGHT_A_GPIO         2
#define LIGHT_B_GPIO         4
#define LIGHT_C_GPIO         5
#define BUTTON_A_GPIO        0
#define BUTTON_B_GPIO        15
#define PERIOD_US            (10 * 1000)
#define DEFAULT_TIMEOUT_A_MS 3000u
#define DEFAULT_TIMEOUT_B_MS 6000u
#define DEFAULT_TIMEOUT_C_MS 9000u

typedef struct {
    rx_pn_net *auto_a;
    rx_pn_net *auto_b;
    rx_pn_net *auto_c;
} app_data;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static const char *light_state(const rx_pn_net *net) {
    return (net != NULL && net->places[1] > 0) ? "ON" : "OFF";
}

static rx_pn_net *net_for_id(app_data *app, char id) {
    switch (id) {
        case 'a': case 'A': return app->auto_a;
        case 'b': case 'B': return app->auto_b;
        case 'c': case 'C': return app->auto_c;
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
        "A: %s  B: %s  C: %s\n",
        light_state(app->auto_a),
        light_state(app->auto_b),
        light_state(app->auto_c)
    );
    printf(
        "auto-off(ms): A=%u  B=%u  C=%u\n",
        auto_pn_get_timeout_ms(app->auto_a),
        auto_pn_get_timeout_ms(app->auto_b),
        auto_pn_get_timeout_ms(app->auto_c)
    );
}

static void
cmd_timeout(rx_fsm_context *ctx, cli_machine_data *cli,
            const char *command, void *command_user_data)
{
    app_data *app = (app_data *)command_user_data;
    char auto_id = '\0';
    unsigned int timeout_ms = 0u;
    char extra = '\0';
    rx_pn_net *net;

    (void)ctx; (void)cli;

    if (sscanf(command, "timeout %c %u %c", &auto_id, &timeout_ms, &extra) != 2) {
        printf("usage: timeout <a|b|c> <ms>\n");
        return;
    }

    net = net_for_id(app, auto_id);
    if (net == NULL) {
        printf("invalid light '%c' (use a, b or c)\n", auto_id);
        return;
    }

    if (timeout_ms == 0u) {
        printf("timeout must be > 0 ms\n");
        return;
    }

    if (auto_pn_set_timeout_ms(net, timeout_ms) != 0) {
        printf("failed to update timeout for light %c\n", auto_id);
        return;
    }

    printf("light %c auto-off set to %u ms\n", auto_id, timeout_ms);
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
    printf(" - timeout <a|b|c> <ms>\n");
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
/* Timing helpers                                                       */
/* ------------------------------------------------------------------ */

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
    if (a.tv_sec != b.tv_sec) {
        return a.tv_sec < b.tv_sec ? -1 : 1;
    }
    if (a.tv_nsec != b.tv_nsec) {
        return a.tv_nsec < b.tv_nsec ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main(void)
{
    rx_pn_runtime pn_runtime;
    rx_fsm_runtime fsm_runtime;

    rx_pn_net auto_a, auto_b, auto_c;
    rx_fsm_machine cli_machine;

    app_data app = {&auto_a, &auto_b, &auto_c};
    cli_machine_data cli_data;

    struct timespec now, next_tick;

    if (rx_pn_runtime_init(&pn_runtime, 3) != 0) {
        fprintf(stderr, "rx_pn_runtime_init failed\n");
        return 1;
    }
    if (rx_fsm_runtime_init(&fsm_runtime, 1) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        rx_pn_runtime_free(&pn_runtime);
        return 1;
    }

    if (auto_pn_init(&auto_a, BUTTON_A_GPIO, LIGHT_A_GPIO, DEFAULT_TIMEOUT_A_MS) != 0 ||
        auto_pn_init(&auto_b, BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_TIMEOUT_B_MS) != 0 ||
        auto_pn_init(&auto_c, BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS) != 0) {
        fprintf(stderr, "auto_pn_init failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }

    if (rx_pn_runtime_add_net(&pn_runtime, &auto_a, 0, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_runtime, &auto_b, 0, 0) != 0 ||
        rx_pn_runtime_add_net(&pn_runtime, &auto_c, 0, 0) != 0) {
        fprintf(stderr, "rx_pn_runtime_add_net failed\n");
        rx_pn_net_free(&auto_a);
        rx_pn_net_free(&auto_b);
        rx_pn_net_free(&auto_c);
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
        cli_fsm_register_command(&cli_data, "timeout", cmd_timeout,  &app) != 0 ||
        cli_fsm_register_command(&cli_data, "help",    cmd_help,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "quit",    cmd_quit,     NULL) != 0 ||
        cli_fsm_register_command(&cli_data, "exit",    cmd_quit,     NULL) != 0) {
        fprintf(stderr, "cli_fsm_register_command failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }
    cli_fsm_create(&cli_machine, "cli", &cli_data);

    if (rx_fsm_runtime_add_machine(&fsm_runtime, &cli_machine, 0, 0) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }

    cmd_help(&fsm_runtime.context, &cli_data, "help", NULL);
    cmd_status(&fsm_runtime.context, &cli_data, "status", &app);
    cli_fsm_print_prompt(&cli_data);

    if (clock_gettime(CLOCK_MONOTONIC, &next_tick) != 0) {
        fprintf(stderr, "clock_gettime failed\n");
        rx_pn_runtime_free(&pn_runtime);
        rx_fsm_runtime_free(&fsm_runtime);
        return 1;
    }
    next_tick = timespec_add_us(next_tick, PERIOD_US);

    while (1) {
        if (rx_fsm_tick(&fsm_runtime) != 0) {
            fprintf(stderr, "rx_fsm_tick failed\n");
            break;
        }
        if (rx_pn_tick(&pn_runtime) != 0) {
            fprintf(stderr, "rx_pn_tick failed\n");
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
                .tv_sec  = next_tick.tv_sec  - now.tv_sec,
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

    rx_pn_net_free(&auto_a);
    rx_pn_net_free(&auto_b);
    rx_pn_net_free(&auto_c);
    rx_pn_runtime_free(&pn_runtime);
    rx_fsm_runtime_free(&fsm_runtime);
    return 0;
}
