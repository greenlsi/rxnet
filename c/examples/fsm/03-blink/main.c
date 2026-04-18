#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rxnet/fsm.h"

#include "blink_fsm.h"

#define LIGHT_A_GPIO GPIO_NUM_2
#define LIGHT_B_GPIO GPIO_NUM_4
#define LIGHT_C_GPIO GPIO_NUM_5
#define BUTTON_A_GPIO GPIO_NUM_0
#define BUTTON_B_GPIO GPIO_NUM_15
#define BLINK_HZ_A 1u
#define BLINK_HZ_B 2u
#define BLINK_HZ_C 3u

static const char *TAG = "rxnet_blink";

void app_main(void) {
    rx_fsm_runtime runtime;
    rx_fsm_machine blink_a_machine;
    rx_fsm_machine blink_b_machine;
    rx_fsm_machine blink_c_machine;

    TickType_t last_wake;
    const TickType_t period_ticks = pdMS_TO_TICKS(10);

    if (rx_fsm_runtime_init(&runtime, 3) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_init failed");
        return;
    }

    blink_fsm_create(&blink_a_machine, BUTTON_A_GPIO, LIGHT_A_GPIO, BLINK_HZ_A);
    blink_fsm_create(&blink_b_machine, BUTTON_A_GPIO, LIGHT_B_GPIO, BLINK_HZ_B);
    blink_fsm_create(&blink_c_machine, BUTTON_B_GPIO, LIGHT_C_GPIO, BLINK_HZ_C);

    if (rx_fsm_runtime_add_machine(&runtime, &blink_a_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &blink_b_machine, 0, 0) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &blink_c_machine, 0, 0) != 0) {
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
