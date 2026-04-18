#pragma once

#include "rxnet/fsm.h"
#include "app_driver.h"

void blink_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int base_hz
);

int blink_fsm_set_base_hz(rx_fsm_machine *machine, unsigned int base_hz);
unsigned int blink_fsm_get_base_hz(const rx_fsm_machine *machine);
int blink_fsm_get_output_enabled(const rx_fsm_machine *machine);
