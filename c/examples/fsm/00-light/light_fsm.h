#pragma once

#include "rxnet/fsm.h"
#include "app_driver.h"

void light_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio
);
