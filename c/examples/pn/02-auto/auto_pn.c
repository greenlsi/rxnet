// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "auto_pn.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "rxnet/config.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

enum {
    P_OFF          = 0,
    P_ON           = 1,
    P_REQUEST      = 2,
    P_AUTO_OFF_DUE = 3,
};

#define AUTO_PLACE_COUNT      4u
#define AUTO_TRANSITION_COUNT 3u

typedef struct {
    int in_use;
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    int latched_event;
    unsigned int auto_off_timeout_ms;
    uint64_t now_ms;
    uint64_t wait_end_ms;
    int wait_active;
    rx_pn_net *net;
} auto_pn_data;

static auto_pn_data s_data[RXNET_MAX_RUNTIME_NODES];

static uint64_t auto_now_ms(void) {
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
#endif
}

static auto_pn_data *find_data(const rx_pn_net *net) {
    size_t i;
    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {
        if (s_data[i].in_use && s_data[i].net == net) {
            return &s_data[i];
        }
    }
    return NULL;
}

static auto_pn_data *find_or_alloc(rx_pn_net *net) {
    size_t i;
    auto_pn_data *data = find_data(net);
    if (data != NULL) {
        return data;
    }
    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {
        if (!s_data[i].in_use) {
            memset(&s_data[i], 0, sizeof(s_data[i]));
            s_data[i].in_use = 1;
            s_data[i].net = net;
            return &s_data[i];
        }
    }
    return NULL;
}

static void auto_pn_latch_inputs(rx_pn_context *ctx, void *user) {
    auto_pn_data *data = (auto_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->now_ms = auto_now_ms();
    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    if (data->latched_event) {
        data->net->places[P_REQUEST]++;
    }

    /* AUTO_OFF_DUE is a signal place: set fresh each tick. */
    data->net->places[P_AUTO_OFF_DUE] =
        (data->net->places[P_ON] > 0 &&
         data->wait_active &&
         data->auto_off_timeout_ms > 0u &&
         data->now_ms >= data->wait_end_ms) ? 1 : 0;
}

static void auto_pn_dump_outputs(rx_pn_context *ctx, void *user) {
    auto_pn_data *data = (auto_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->net->places[P_ON] > 0);
    if (data->latched_event) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

/* Deferred action: start or reset the auto-off countdown. */
static void action_start_timer(rx_pn_context *ctx, void *user) {
    auto_pn_data *data = (auto_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->wait_end_ms = data->now_ms + (uint64_t)data->auto_off_timeout_ms;
    data->wait_active = 1;
}

/*
 * Transitions
 *   T_TURN_ON  — off + request  → on      (starts timer)
 *   T_REFRESH  — on  + request  → on      (resets timer)
 *   T_AUTO_OFF — on  + due      → off
 */
static const rx_pn_arc t_turn_on_consume[]  = {{P_OFF, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_turn_on_produce[]  = {{P_ON, 1}};
static const rx_pn_arc t_refresh_consume[]  = {{P_ON, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_refresh_produce[]  = {{P_ON, 1}};
static const rx_pn_arc t_auto_off_consume[] = {{P_ON, 1}, {P_AUTO_OFF_DUE, 1}};
static const rx_pn_arc t_auto_off_produce[] = {{P_OFF, 1}};

static const rx_pn_transition transitions[] = {
    {t_turn_on_consume,  2, t_turn_on_produce,  1, NULL, action_start_timer},
    {t_refresh_consume,  2, t_refresh_produce,  1, NULL, action_start_timer},
    {t_auto_off_consume, 2, t_auto_off_produce, 1, NULL, NULL},
};

static const int initial_places[] = {1, 0, 0, 0};

int auto_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int auto_off_timeout_ms
) {
    auto_pn_data *data;

    if (net == NULL || auto_off_timeout_ms == 0u) {
        return -1;
    }

    data = find_or_alloc(net);
    if (data == NULL) {
        return -1;
    }

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        data->in_use = 0;
        return -1;
    }

    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = 0;
    data->auto_off_timeout_ms = auto_off_timeout_ms;
    data->now_ms = 0;
    data->wait_end_ms = 0;
    data->wait_active = 0;

    if (rx_pn_net_init(net, "auto", initial_places, AUTO_PLACE_COUNT,
                       transitions, AUTO_TRANSITION_COUNT, data,
                       auto_pn_latch_inputs, auto_pn_dump_outputs) != 0) {
        data->in_use = 0;
        return -1;
    }

    return 0;
}

int auto_pn_set_timeout_ms(rx_pn_net *net, unsigned int timeout_ms) {
    auto_pn_data *data = find_data(net);

    if (data == NULL || timeout_ms == 0u) {
        return -1;
    }

    data->auto_off_timeout_ms = timeout_ms;
    if (net->places[P_ON] > 0) {
        data->wait_end_ms = auto_now_ms() + (uint64_t)timeout_ms;
        data->wait_active = 1;
    }
    return 0;
}

unsigned int auto_pn_get_timeout_ms(const rx_pn_net *net) {
    const auto_pn_data *data = find_data(net);

    return data != NULL ? data->auto_off_timeout_ms : 0u;
}
