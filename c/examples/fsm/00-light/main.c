#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rxnet/fsm.h"

#include "app_driver.h"
#include "light_fsm.h"

#define LIGHT_GPIO GPIO_NUM_2
#define BUTTON_GPIO GPIO_NUM_0

static const char *TAG = "rxnet_light";

void app_main(void) {
    rx_fsm_runtime runtime;
    rx_fsm_machine light_machine;
    app_driver driver;
    light_inputs *inputs;

    TickType_t last_wake;
    const TickType_t period_ticks = pdMS_TO_TICKS(10);

    if (rx_fsm_runtime_init(&runtime, sizeof(light_inputs), 1) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_init failed");
        return;
    }

    inputs = (light_inputs *)runtime.context.inputs;

    if (app_driver_init(&driver, LIGHT_GPIO, BUTTON_GPIO, inputs) != ESP_OK) {
        ESP_LOGE(TAG, "app_driver_init failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    light_fsm_create(&light_machine, &driver);

    if (rx_fsm_runtime_add_machine(&runtime, &light_machine) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_add_machine failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    last_wake = xTaskGetTickCount();

    while (1) {
        if (rx_fsm_tick(&runtime) != 0) {
            ESP_LOGE(TAG, "rx_fsm_tick failed");
        }

        inputs->button_press_event = false;
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}
