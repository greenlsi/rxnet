#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include "light_fsm.h"

typedef struct {
    gpio_num_t light_gpio;
    gpio_num_t button_gpio;
    light_inputs *inputs;
} app_driver;

esp_err_t app_driver_init(
    app_driver *driver,
    gpio_num_t light_gpio,
    gpio_num_t button_gpio,
    light_inputs *inputs
);
void app_driver_set_light(app_driver *driver, int enabled);
