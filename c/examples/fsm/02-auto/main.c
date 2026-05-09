// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rxnet/fsm.h"

#include "auto_fsm.h"

#define LIGHT_A_GPIO GPIO_NUM_2
#define LIGHT_B_GPIO GPIO_NUM_4
#define LIGHT_C_GPIO GPIO_NUM_5
#define BUTTON_A_GPIO GPIO_NUM_0
#define BUTTON_B_GPIO GPIO_NUM_15
#define AUTO_OFF_A_MS 3000u
#define AUTO_OFF_B_MS 6000u
#define AUTO_OFF_C_MS 9000u

static const char *TAG = "rxnet_light";

void app_main(void) {
    rx_fsm_runtime runtime;
    rx_fsm_machine auto_a_machine;
    rx_fsm_machine auto_b_machine;
    rx_fsm_machine auto_c_machine;

    TickType_t last_wake;
    const TickType_t period_ticks = pdMS_TO_TICKS(10);

    if (rx_fsm_runtime_init(&runtime, 3) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_init failed");
        return;
    }

    auto_fsm_create(&auto_a_machine, BUTTON_A_GPIO, LIGHT_A_GPIO, AUTO_OFF_A_MS);
    auto_fsm_create(&auto_b_machine, BUTTON_A_GPIO, LIGHT_B_GPIO, AUTO_OFF_B_MS);
    auto_fsm_create(&auto_c_machine, BUTTON_B_GPIO, LIGHT_C_GPIO, AUTO_OFF_C_MS);

    if (rx_fsm_runtime_add_machine(&runtime, &auto_a_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &auto_b_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &auto_c_machine, 0, 0) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_add_machine failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    last_wake = xTaskGetTickCount();

    while (1) {
        if (rx_fsm_tick(&runtime) != 0) {
            ESP_LOGE(TAG, "rx_fsm_tick failed");
        }
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}
