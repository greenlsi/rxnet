#include "rxnet.h"

#include <stdio.h>

enum {
    START = 0,
    STOP = 1,
};

enum {
    IDLE = 0,
    RUNNING = 1,
};

typedef struct {
    int motor_enabled;
    int lamp_enabled;
} app_data;

static const char *state_name(int state) {
    return state == RUNNING ? "RUNNING" : "IDLE";
}

static int start_pressed(const rx_context *ctx, void *user) {
    (void)user;
    return rx_context_read_input(ctx, START) != 0;
}

static int stop_pressed(const rx_context *ctx, void *user) {
    (void)user;
    return rx_context_read_input(ctx, STOP) != 0;
}

static void motor_on(rx_context *ctx, void *user) {
    (void)ctx;
    ((app_data *)user)->motor_enabled = 1;
}

static void motor_off(rx_context *ctx, void *user) {
    (void)ctx;
    ((app_data *)user)->motor_enabled = 0;
}

static void lamp_on(rx_context *ctx, void *user) {
    (void)ctx;
    ((app_data *)user)->lamp_enabled = 1;
}

static void lamp_off(rx_context *ctx, void *user) {
    (void)ctx;
    ((app_data *)user)->lamp_enabled = 0;
}

static void print_status(const char *step, const rx_machine *motor, const rx_machine *lamp, const app_data *app) {
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
    app_data app = {0, 0};
    rx_runtime runtime;

    rx_transition motor_transitions[] = {
        {IDLE, RUNNING, start_pressed, motor_on},
        {RUNNING, IDLE, stop_pressed, motor_off},
    };

    rx_transition lamp_transitions[] = {
        {IDLE, RUNNING, start_pressed, lamp_on},
        {RUNNING, IDLE, stop_pressed, lamp_off},
    };

    rx_machine motor;
    rx_machine lamp;

    if (rx_runtime_init(&runtime, 2, 2) != 0) {
        fprintf(stderr, "rx_runtime_init failed\n");
        return 1;
    }

    rx_machine_init(&motor, "motor", IDLE, motor_transitions, 2, &app);
    rx_machine_init(&lamp, "lamp", IDLE, lamp_transitions, 2, &app);

    if (rx_runtime_add_machine(&runtime, &motor) != 0 || rx_runtime_add_machine(&runtime, &lamp) != 0) {
        fprintf(stderr, "rx_runtime_add_machine failed\n");
        rx_runtime_free(&runtime);
        return 1;
    }

    print_status("init", &motor, &lamp, &app);

    rx_context_stage_input(&runtime.context, START, 1);
    rx_context_stage_input(&runtime.context, STOP, 0);
    rx_tick(&runtime);
    print_status("after start", &motor, &lamp, &app);

    rx_context_stage_input(&runtime.context, START, 0);
    rx_context_stage_input(&runtime.context, STOP, 1);
    rx_tick(&runtime);
    print_status("after stop", &motor, &lamp, &app);

    rx_runtime_free(&runtime);
    return 0;
}
