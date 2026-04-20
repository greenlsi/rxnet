// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "auto_fsm.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "rxnet/config.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

typedef struct {
    rx_fsm_machine *machine;   /* needed to read machine->state in callbacks */
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    bool latched_event;
    bool event_consumed;
    int output_enabled;
    unsigned int auto_off_timeout_ms;
    uint64_t now_ms;
    uint64_t wait_end_ms;
    bool wait_active;
} auto_machine_data;

static auto_machine_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

enum {
    AUTO_STATE_OFF = 0,
    AUTO_STATE_ON = 1,
};

static uint64_t auto_now_ms(void) {
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
#endif
}

static void auto_machine_latch_inputs(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;
    uint64_t now_ms;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    now_ms = auto_now_ms();
    data->now_ms = now_ms;
    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    data->event_consumed = false;

    if (data->latched_event) {
        data->wait_end_ms = now_ms + (uint64_t)data->auto_off_timeout_ms;
        data->wait_active = true;
    }

    if (data->machine->state == AUTO_STATE_OFF) {
        data->wait_active = false;
    }
}

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const auto_machine_data *data = (const auto_machine_data *)user;

    (void)ctx;
    return data != NULL && data->latched_event;
}

static int auto_off_elapsed(const rx_fsm_context *ctx, void *user) {
    const auto_machine_data *data = (const auto_machine_data *)user;

    (void)ctx;
    return data != NULL &&
           data->wait_active &&
           data->auto_off_timeout_ms > 0u &&
           data->now_ms >= data->wait_end_ms;
}

static void auto_on(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = 1;
    data->event_consumed = true;
    if (!data->wait_active) {
        data->wait_end_ms = auto_now_ms() + (uint64_t)data->auto_off_timeout_ms;
        data->wait_active = true;
    }
}

static void auto_off(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = 0;
    data->wait_active = false;
}

static void auto_machine_dump_outputs(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->output_enabled);
    if (data->event_consumed) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

void auto_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int auto_off_timeout_ms
) {
    static const rx_fsm_transition transitions[] = {
        {AUTO_STATE_OFF, AUTO_STATE_ON, button_pressed, auto_on},
        {AUTO_STATE_ON, AUTO_STATE_ON, button_pressed, auto_on},
        {AUTO_STATE_ON, AUTO_STATE_OFF, auto_off_elapsed, auto_off},
    };
    auto_machine_data *data;

    if (machine == NULL || auto_off_timeout_ms == 0u ||
        s_count >= RXNET_MAX_RUNTIME_NODES) {
        return;
    }
    data = &s_data[s_count++];

    data->machine = machine;
    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = false;
    data->event_consumed = false;
    data->output_enabled = 0;
    data->auto_off_timeout_ms = auto_off_timeout_ms;
    data->now_ms = 0;
    data->wait_end_ms = 0;
    data->wait_active = false;

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        --s_count;
        return;
    }

    rx_fsm_machine_init(
        machine,
        "auto",
        AUTO_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data,
        auto_machine_latch_inputs,
        auto_machine_dump_outputs
    );
}

int auto_fsm_set_auto_off_timeout_ms(rx_fsm_machine *machine, unsigned int auto_off_timeout_ms) {
    auto_machine_data *data;

    if (machine == NULL || machine->user == NULL || auto_off_timeout_ms == 0u) {
        return -1;
    }
    data = (auto_machine_data *)machine->user;
    data->auto_off_timeout_ms = auto_off_timeout_ms;
    if (machine->state == AUTO_STATE_ON) {
        data->wait_end_ms = auto_now_ms() + (uint64_t)auto_off_timeout_ms;
        data->wait_active = true;
    }
    return 0;
}

unsigned int auto_fsm_get_auto_off_timeout_ms(const rx_fsm_machine *machine) {
    if (machine == NULL || machine->user == NULL) {
        return 0u;
    }
    return ((const auto_machine_data *)machine->user)->auto_off_timeout_ms;
}
