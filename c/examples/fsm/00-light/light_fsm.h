#pragma once

#include <stdbool.h>

#include "rxnet/fsm.h"

typedef struct {
    bool button_a_press_event;
    bool button_b_press_event;
} light_inputs;

typedef struct {
    bool button_press_event;
} light_local_inputs;

typedef enum {
    LIGHT_BUTTON_A = 0,
    LIGHT_BUTTON_B = 1,
} light_button_source;

typedef void (*light_set_output_fn)(void *output_user, int enabled);

typedef struct {
    light_button_source button_source;
    light_set_output_fn set_output;
    void *output_user;
    light_local_inputs in;
} light_machine_data;

void light_fsm_create(rx_fsm_machine *machine, const char *name, light_machine_data *data);
