// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_err.h"
#else
typedef int gpio_num_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG (-1)
#define ESP_ERR_NO_MEM (-2)
#endif

esp_err_t app_driver_init_button(gpio_num_t button_gpio);
esp_err_t app_driver_init_light(gpio_num_t light_gpio);
void app_driver_set_light(gpio_num_t light_gpio, int enabled);
esp_err_t app_driver_trigger_button(gpio_num_t button_gpio);
bool app_driver_latch_button_event(gpio_num_t button_gpio);
void app_driver_clear_button_event(gpio_num_t button_gpio);
