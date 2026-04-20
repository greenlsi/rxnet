// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "app_driver.h"

#include <stdio.h>
#include <stdint.h>

#define APP_DRIVER_MAX_GPIO 64

static unsigned char s_button_registered[APP_DRIVER_MAX_GPIO];
static unsigned char s_button_event[APP_DRIVER_MAX_GPIO];
static unsigned char s_light_initialized[APP_DRIVER_MAX_GPIO];
static unsigned char s_light_level[APP_DRIVER_MAX_GPIO];

static int gpio_to_index(gpio_num_t gpio) {
    int idx = (int)gpio;

    if (idx < 0 || idx >= APP_DRIVER_MAX_GPIO) {
        return -1;
    }

    return idx;
}

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_driver_lock = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR button_isr_handler(void *arg) {
    int idx = (int)(intptr_t)arg;

    if (idx < 0 || idx >= APP_DRIVER_MAX_GPIO) {
        return;
    }

    portENTER_CRITICAL_ISR(&s_driver_lock);
    if (s_button_registered[idx]) {
        s_button_event[idx] = 1;
    }
    portEXIT_CRITICAL_ISR(&s_driver_lock);
}

esp_err_t app_driver_init_button(gpio_num_t button_gpio) {
    gpio_config_t button_config;
    esp_err_t err;
    int idx = gpio_to_index(button_gpio);

    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_button_registered[idx]) {
        return ESP_OK;
    }

    button_config.pin_bit_mask = 1ULL << button_gpio;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_config.intr_type = GPIO_INTR_NEGEDGE;

    err = gpio_config(&button_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(button_gpio, button_isr_handler, (void *)(intptr_t)idx);
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_driver_lock);
    s_button_registered[idx] = 1;
    s_button_event[idx] = 0;
    portEXIT_CRITICAL(&s_driver_lock);
    return ESP_OK;
}

esp_err_t app_driver_init_light(gpio_num_t light_gpio) {
    gpio_config_t light_config;
    esp_err_t err;
    int idx = gpio_to_index(light_gpio);

    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    light_config.pin_bit_mask = 1ULL << light_gpio;
    light_config.mode = GPIO_MODE_OUTPUT;
    light_config.pull_up_en = GPIO_PULLUP_DISABLE;
    light_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    light_config.intr_type = GPIO_INTR_DISABLE;

    err = gpio_config(&light_config);
    if (err != ESP_OK) {
        return err;
    }

    s_light_initialized[idx] = 1;
    s_light_level[idx] = 0;
    return gpio_set_level(light_gpio, 0);
}

void app_driver_set_light(gpio_num_t light_gpio, int enabled) {
    int idx = gpio_to_index(light_gpio);
    unsigned char level = enabled ? 1u : 0u;

    if (idx < 0) {
        return;
    }

    if (s_light_initialized[idx] && s_light_level[idx] == level) {
        return;
    }

    s_light_initialized[idx] = 1;
    s_light_level[idx] = level;
    gpio_set_level(light_gpio, (int)level);
}

esp_err_t app_driver_trigger_button(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_driver_lock);
    if (!s_button_registered[idx]) {
        portEXIT_CRITICAL(&s_driver_lock);
        return ESP_ERR_INVALID_ARG;
    }
    s_button_event[idx] = 1;
    portEXIT_CRITICAL(&s_driver_lock);
    return ESP_OK;
}

bool app_driver_latch_button_event(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);
    bool value;

    if (idx < 0) {
        return false;
    }

    portENTER_CRITICAL(&s_driver_lock);
    value = s_button_registered[idx] != 0 && s_button_event[idx] != 0;
    portEXIT_CRITICAL(&s_driver_lock);
    return value;
}

void app_driver_clear_button_event(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0) {
        return;
    }

    portENTER_CRITICAL(&s_driver_lock);
    if (s_button_registered[idx]) {
        s_button_event[idx] = 0;
    }
    portEXIT_CRITICAL(&s_driver_lock);
}

#else

esp_err_t app_driver_init_button(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_button_registered[idx] = 1;
    s_button_event[idx] = 0;
    return ESP_OK;
}

esp_err_t app_driver_init_light(gpio_num_t light_gpio) {
    int idx = gpio_to_index(light_gpio);

    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_light_initialized[idx] = 1;
    s_light_level[idx] = 0;
    printf("[app_driver] init light GPIO %d (host mock)\n", (int)light_gpio);
    return ESP_OK;
}

void app_driver_set_light(gpio_num_t light_gpio, int enabled) {
    int idx = gpio_to_index(light_gpio);
    unsigned char level = enabled ? 1u : 0u;

    if (idx < 0) {
        return;
    }

    if (s_light_initialized[idx] && s_light_level[idx] == level) {
        return;
    }

    s_light_initialized[idx] = 1;
    s_light_level[idx] = level;
    printf("[app_driver] light GPIO %d -> %s\n", (int)light_gpio, level ? "ON" : "OFF");
}

esp_err_t app_driver_trigger_button(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0 || !s_button_registered[idx]) {
        return ESP_ERR_INVALID_ARG;
    }

    s_button_event[idx] = 1;
    return ESP_OK;
}

bool app_driver_latch_button_event(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0 || !s_button_registered[idx]) {
        return false;
    }

    return s_button_event[idx] != 0;
}

void app_driver_clear_button_event(gpio_num_t button_gpio) {
    int idx = gpio_to_index(button_gpio);

    if (idx < 0 || !s_button_registered[idx]) {
        return;
    }

    s_button_event[idx] = 0;
}

#endif
