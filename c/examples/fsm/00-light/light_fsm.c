#include "light_fsm.h"

#include <stddef.h>

enum {
    LIGHT_STATE_OFF = 0,
    LIGHT_STATE_ON = 1,
};

static void project_inputs(const rx_fsm_context *ctx, void *user) {
    const light_inputs *global_in = (const light_inputs *)ctx->latched_inputs;
    light_machine_data *data = (light_machine_data *)user;

    if (data->button_source == LIGHT_BUTTON_B) {
        data->in.button_press_event = global_in->button_b_press_event;
    } else {
        data->in.button_press_event = global_in->button_a_press_event;
    }
}

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const light_machine_data *data = (const light_machine_data *)user;
    (void)ctx;
    return data->in.button_press_event;
}

static void light_on(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;
    (void)ctx;
    if (data->set_output != NULL) {
        data->set_output(data->output_user, 1);
    }
}

static void light_off(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;
    (void)ctx;
    if (data->set_output != NULL) {
        data->set_output(data->output_user, 0);
    }
}

void light_fsm_create(rx_fsm_machine *machine, const char *name, light_machine_data *data) {
    static const rx_fsm_transition transitions[] = {
        {LIGHT_STATE_OFF, LIGHT_STATE_ON, button_pressed, light_on},
        {LIGHT_STATE_ON, LIGHT_STATE_OFF, button_pressed, light_off},
    };

    rx_fsm_machine_init(
        machine,
        name,
        LIGHT_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data
    );
    rx_fsm_machine_set_inputs_projector(machine, project_inputs);
}
