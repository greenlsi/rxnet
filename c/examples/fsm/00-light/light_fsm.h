#pragma once

#include <stdbool.h>

#include "rxnet/fsm.h"

typedef struct {
    bool button_press_event;
} light_inputs;

void light_fsm_create(rx_fsm_machine *machine, void *user);
