#include "app_driver.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_driver_lock = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR button_isr_handler(void *arg) {
    app_driver *driver = (app_driver *)arg;

    if (driver == NULL || driver->inputs == NULL) {
        return;
    }

    portENTER_CRITICAL_ISR(&s_driver_lock);
    driver->inputs->button_press_event = true;
    portEXIT_CRITICAL_ISR(&s_driver_lock);
}

esp_err_t app_driver_init(
    app_driver *driver,
    gpio_num_t light_gpio,
    gpio_num_t button_gpio,
    light_inputs *inputs
) {
    gpio_config_t light_config;
    gpio_config_t button_config;
    esp_err_t err;

    if (driver == NULL || inputs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    driver->light_gpio = light_gpio;
    driver->button_gpio = button_gpio;
    driver->inputs = inputs;
    driver->inputs->button_press_event = false;

    light_config.pin_bit_mask = 1ULL << light_gpio;
    light_config.mode = GPIO_MODE_OUTPUT;
    light_config.pull_up_en = GPIO_PULLUP_DISABLE;
    light_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    light_config.intr_type = GPIO_INTR_DISABLE;

    button_config.pin_bit_mask = 1ULL << button_gpio;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_config.intr_type = GPIO_INTR_NEGEDGE;

    err = gpio_config(&light_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_config(&button_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(light_gpio, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(button_gpio, button_isr_handler, driver);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

void app_driver_set_light(app_driver *driver, int enabled) {
    if (driver == NULL) {
        return;
    }

    gpio_set_level(driver->light_gpio, enabled ? 1 : 0);
}
