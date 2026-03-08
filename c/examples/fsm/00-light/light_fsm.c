#include "light_fsm.h"

#include <stddef.h>

#include "app_driver.h"

enum {
    LIGHT_STATE_OFF = 0,
    LIGHT_STATE_ON = 1,
};

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const light_inputs *in = (const light_inputs *)ctx->latched_inputs;
    (void)user;
    return in->button_press_event;
}

static void light_on(rx_fsm_context *ctx, void *user) {
    app_driver *driver = (app_driver *)user;
    (void)ctx;
    app_driver_set_light(driver, 1);
}

static void light_off(rx_fsm_context *ctx, void *user) {
    app_driver *driver = (app_driver *)user;
    (void)ctx;
    app_driver_set_light(driver, 0);
}

void light_fsm_create(rx_fsm_machine *machine, void *user) {
    static const rx_fsm_transition transitions[] = {
        {LIGHT_STATE_OFF, LIGHT_STATE_ON, button_pressed, light_on},
        {LIGHT_STATE_ON, LIGHT_STATE_OFF, button_pressed, light_off},
    };

    rx_fsm_machine_init(
        machine,
        "light",
        LIGHT_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        user
    );
}
