#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rxnet/fsm.h"

#include "app_driver.h"
#include "light_fsm.h"

#define LIGHT_A_GPIO GPIO_NUM_2
#define LIGHT_B_GPIO GPIO_NUM_4
#define LIGHT_C_GPIO GPIO_NUM_5
#define BUTTON_A_GPIO GPIO_NUM_0
#define BUTTON_B_GPIO GPIO_NUM_15

static const char *TAG = "rxnet_light";

typedef struct {
    const app_driver *driver;
    gpio_num_t gpio;
} esp_light_output;

static void esp_set_light_output(void *output_user, int enabled) {
    esp_light_output *out = (esp_light_output *)output_user;
    app_driver_set_light(out->driver, out->gpio, enabled);
}

void app_main(void) {
    rx_fsm_runtime runtime;
    rx_fsm_machine light_a_machine;
    rx_fsm_machine light_b_machine;
    rx_fsm_machine light_c_machine;
    app_driver button_a_driver;
    app_driver button_b_driver;
    light_inputs inputs = {0};
    light_machine_data light_a_data = {0};
    light_machine_data light_b_data = {0};
    light_machine_data light_c_data = {0};
    esp_light_output out_a = {0};
    esp_light_output out_b = {0};
    esp_light_output out_c = {0};

    TickType_t last_wake;
    const TickType_t period_ticks = pdMS_TO_TICKS(10);

    if (rx_fsm_runtime_init(&runtime, &inputs, sizeof(inputs), 3) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_init failed");
        return;
    }

    if (app_driver_init_button(&button_a_driver, BUTTON_A_GPIO, &inputs, LIGHT_BUTTON_A) != ESP_OK ||
        app_driver_init_button(&button_b_driver, BUTTON_B_GPIO, &inputs, LIGHT_BUTTON_B) != ESP_OK) {
        ESP_LOGE(TAG, "app_driver_init_button failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    if (app_driver_init_light(LIGHT_A_GPIO) != ESP_OK ||
        app_driver_init_light(LIGHT_B_GPIO) != ESP_OK ||
        app_driver_init_light(LIGHT_C_GPIO) != ESP_OK) {
        ESP_LOGE(TAG, "app_driver_init_light failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    light_a_data.button_source = LIGHT_BUTTON_A;
    light_b_data.button_source = LIGHT_BUTTON_A;
    light_c_data.button_source = LIGHT_BUTTON_B;
    light_a_data.set_output = esp_set_light_output;
    light_b_data.set_output = esp_set_light_output;
    light_c_data.set_output = esp_set_light_output;

    out_a.driver = &button_a_driver;
    out_a.gpio = LIGHT_A_GPIO;
    out_b.driver = &button_a_driver;
    out_b.gpio = LIGHT_B_GPIO;
    out_c.driver = &button_b_driver;
    out_c.gpio = LIGHT_C_GPIO;

    light_a_data.output_user = &out_a;
    light_b_data.output_user = &out_b;
    light_c_data.output_user = &out_c;

    light_fsm_create(&light_a_machine, "light-a", &light_a_data);
    light_fsm_create(&light_b_machine, "light-b", &light_b_data);
    light_fsm_create(&light_c_machine, "light-c", &light_c_data);

    if (rx_fsm_runtime_add_machine(&runtime, &light_a_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_b_machine) != 0 ||
        rx_fsm_runtime_add_machine(&runtime, &light_c_machine) != 0) {
        ESP_LOGE(TAG, "rx_fsm_runtime_add_machine failed");
        rx_fsm_runtime_free(&runtime);
        return;
    }

    last_wake = xTaskGetTickCount();

    while (1) {
        if (rx_fsm_tick(&runtime) != 0) {
            ESP_LOGE(TAG, "rx_fsm_tick failed");
        }

        inputs.button_a_press_event = false;
        inputs.button_b_press_event = false;
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}
