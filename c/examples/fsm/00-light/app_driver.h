#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include "light_fsm.h"

typedef struct app_driver {
    gpio_num_t button_gpio;
    light_inputs *inputs;
    light_button_source button_source;
} app_driver;

esp_err_t app_driver_init_button(
    app_driver *driver,
    gpio_num_t button_gpio,
    light_inputs *inputs,
    light_button_source button_source
);
esp_err_t app_driver_init_light(gpio_num_t light_gpio);
void app_driver_set_light(const app_driver *driver, gpio_num_t light_gpio, int enabled);
