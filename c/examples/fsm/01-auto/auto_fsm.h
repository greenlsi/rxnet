#pragma once

#include "rxnet/fsm.h"
#include "app_driver.h"

void auto_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t auto_gpio,
    unsigned int auto_off_timeout_ms
);

int auto_fsm_set_auto_off_timeout_ms(rx_fsm_machine *machine, unsigned int auto_off_timeout_ms);
unsigned int auto_fsm_get_auto_off_timeout_ms(const rx_fsm_machine *machine);
