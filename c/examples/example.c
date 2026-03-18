#include "rxnet/fsm.h"

#include <stdio.h>

typedef struct {
    int start;
    int stop;
} app_inputs;

enum {
    IDLE = 0,
    RUNNING = 1,
};

typedef struct {
    int motor_enabled;
    int lamp_enabled;
    app_inputs *inputs;
} app_data;

static const char *state_name(int state) {
    return state == RUNNING ? "RUNNING" : "IDLE";
}

static int start_pressed(const rx_fsm_context *ctx, void *user) {
    const app_inputs *in = ((const app_data *)user)->inputs;
    return in->start != 0;
}

static int stop_pressed(const rx_fsm_context *ctx, void *user) {
    const app_inputs *in = ((const app_data *)user)->inputs;
    return in->stop != 0;
}

static void motor_on(rx_fsm_context *ctx, void *user) {
    ((app_data *)user)->motor_enabled = 1;
}

static void motor_off(rx_fsm_context *ctx, void *user) {
    ((app_data *)user)->motor_enabled = 0;
}

static void lamp_on(rx_fsm_context *ctx, void *user) {
    ((app_data *)user)->lamp_enabled = 1;
}

static void lamp_off(rx_fsm_context *ctx, void *user) {
    ((app_data *)user)->lamp_enabled = 0;
}

static void machine_latch_inputs(rx_fsm_context *ctx, void *user) {
}

static void machine_dump_outputs(rx_fsm_context *ctx, void *user) {
}

static void print_status(const char *step, const rx_fsm_machine *motor, const rx_fsm_machine *lamp, const app_data *app) {
    printf(
        "%s: motor_state=%s lamp_state=%s motor_enabled=%d lamp_enabled=%d\n",
        step,
        state_name(motor->state),
        state_name(lamp->state),
        app->motor_enabled,
        app->lamp_enabled
    );
}

int main(void) {
    app_inputs inputs = {0};
    app_data app = {0, 0, &inputs};

    rx_fsm_transition motor_transitions[] = {
        {IDLE, RUNNING, start_pressed, motor_on},
        {RUNNING, IDLE, stop_pressed, motor_off},
    };

    rx_fsm_transition lamp_transitions[] = {
        {IDLE, RUNNING, start_pressed, lamp_on},
        {RUNNING, IDLE, stop_pressed, lamp_off},
    };

    rx_fsm_runtime *runtime;
    rx_fsm_machine *motor;
    rx_fsm_machine *lamp;

    runtime = rx_fsm_runtime_create(2);
    if (runtime == NULL) {
        fprintf(stderr, "rx_fsm_runtime_create failed\n");
        return 1;
    }

    motor = rx_fsm_machine_create("motor", IDLE, motor_transitions, 2, &app, machine_latch_inputs, machine_dump_outputs);
    lamp = rx_fsm_machine_create("lamp", IDLE, lamp_transitions, 2, &app, machine_latch_inputs, machine_dump_outputs);
    if (motor == NULL || lamp == NULL) {
        fprintf(stderr, "rx_fsm_machine_create failed\n");
        rx_fsm_machine_destroy(motor);
        rx_fsm_machine_destroy(lamp);
        rx_fsm_runtime_destroy(runtime);
        return 1;
    }

    if (rx_fsm_runtime_add_machine(runtime, motor) != 0 || rx_fsm_runtime_add_machine(runtime, lamp) != 0) {
        fprintf(stderr, "rx_fsm_runtime_add_machine failed\n");
        rx_fsm_machine_destroy(motor);
        rx_fsm_machine_destroy(lamp);
        rx_fsm_runtime_destroy(runtime);
        return 1;
    }

    print_status("init", motor, lamp, &app);

    inputs.start = 1;
    inputs.stop = 0;
    rx_fsm_tick(runtime);
    print_status("after start", motor, lamp, &app);

    inputs.start = 0;
    inputs.stop = 1;
    rx_fsm_tick(runtime);
    print_status("after stop", motor, lamp, &app);

    rx_fsm_machine_destroy(motor);
    rx_fsm_machine_destroy(lamp);
    rx_fsm_runtime_destroy(runtime);
    return 0;
}
