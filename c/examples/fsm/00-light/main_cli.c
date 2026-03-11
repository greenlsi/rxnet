#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rxnet/fsm.h"

#include "light_fsm.h"

typedef struct {
    const char *name;
    bool *value;
} cli_light_output;

static void cli_set_light_output(void *output_user, int enabled) {
    cli_light_output *out = (cli_light_output *)output_user;
    *out->value = enabled != 0;
}

static const char *state_name(int state) {
    return state == 0 ? "OFF" : "ON";
}

static void print_status(
    const rx_fsm_machine *a,
    const rx_fsm_machine *b,
    const rx_fsm_machine *c,
    bool light_a,
    bool light_b,
    bool light_c
) {
    printf(
        "states: A=%s B=%s C=%s | outputs: A=%d B=%d C=%d\n",
        state_name(a->state),
        state_name(b->state),
        state_name(c->state),
        light_a,
        light_b,
        light_c
    );
}

static int run_tick(rx_fsm_runtime *runtime, light_inputs *inputs) {
    if (rx_fsm_tick(runtime) != 0) {
        fprintf(stderr, "rx_fsm_tick failed\n");
        return -1;
    }

    inputs->button_a_press_event = false;
    inputs->button_b_press_event = false;
    return 0;
}

int main(void) {
    rx_fsm_runtime runtime;
    rx_fsm_machine light_a_machine;
    rx_fsm_machine light_b_machine;
    rx_fsm_machine light_c_machine;
    light_inputs inputs = {0};

    bool light_a = false;
    bool light_b = false;
    bool light_c = false;

    cli_light_output out_a = {"A", &light_a};
    cli_light_output out_b = {"B", &light_b};
    cli_light_output out_c = {"C", &light_c};

    light_machine_data light_a_data = {
        .button_source = LIGHT_BUTTON_A,
        .set_output = cli_set_light_output,
        .output_user = &out_a,
    };
    light_machine_data light_b_data = {
        .button_source = LIGHT_BUTTON_A,
        .set_output = cli_set_light_output,
        .output_user = &out_b,
    };
    light_machine_data light_c_data = {
        .button_source = LIGHT_BUTTON_B,
        .set_output = cli_set_light_output,
        .output_user = &out_c,
    };

    char line[64];

    if (rx_fsm_runtime_init(&runtime, &inputs, sizeof(inputs), 3) != 0) {
        fprintf(stderr, "rx_fsm_runtime_init failed\n");
        return 1;
    }

    light_fsm_create(&light_a_machine, "light-a", &light_a_data);
    light_fsm_create(&light_b_machine, "light-b", &light_b_data);
    light_fsm_create(&light_c_machine, "light-c", &light_c_data);

    if (rx_fsm_runtime_add_machine(&runtime, &light_a_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_b_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_c_machine) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_fsm_runtime_free(&runtime);
        return 1;
    }

    printf("Commands: a, b, tick, status, help, quit\n");
    print_status(&light_a_machine, &light_b_machine, &light_c_machine, light_a, light_b, light_c);

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        if (strncmp(line, "a", 1) == 0 || strncmp(line, "press a", 7) == 0) {
            inputs.button_a_press_event = true;
            if (run_tick(&runtime, &inputs) != 0) {
                break;
            }
            print_status(&light_a_machine, &light_b_machine, &light_c_machine, light_a, light_b, light_c);
            continue;
        }

        if (strncmp(line, "b", 1) == 0 || strncmp(line, "press b", 7) == 0) {
            inputs.button_b_press_event = true;
            if (run_tick(&runtime, &inputs) != 0) {
                break;
            }
            print_status(&light_a_machine, &light_b_machine, &light_c_machine, light_a, light_b, light_c);
            continue;
        }

        if (strncmp(line, "tick", 4) == 0) {
            if (run_tick(&runtime, &inputs) != 0) {
                break;
            }
            print_status(&light_a_machine, &light_b_machine, &light_c_machine, light_a, light_b, light_c);
            continue;
        }

        if (strncmp(line, "status", 6) == 0) {
            print_status(&light_a_machine, &light_b_machine, &light_c_machine, light_a, light_b, light_c);
            continue;
        }

        if (strncmp(line, "help", 4) == 0) {
            printf("Commands: a | b | tick | status | quit\n");
            continue;
        }

        if (strncmp(line, "quit", 4) == 0 || strncmp(line, "exit", 4) == 0) {
            break;
        }

        printf("Unknown command. Use: help\n");
    }

    rx_fsm_runtime_free(&runtime);
    return 0;
}
