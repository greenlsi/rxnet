#include "blink_pn.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "rxnet/config.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

enum {
    P_OFF        = 0,
    P_X1         = 1,
    P_X2         = 2,
    P_REQUEST    = 3,
    P_TOGGLE_DUE = 4,
};

#define BLINK_PLACE_COUNT      5u
#define BLINK_TRANSITION_COUNT 5u

typedef struct {
    int in_use;
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    int latched_event;
    int output_enabled;
    unsigned int base_hz;
    uint64_t now_ms;
    uint64_t next_toggle_ms;
    rx_pn_net *net;
} blink_pn_data;

static blink_pn_data s_data[RXNET_MAX_RUNTIME_NODES];

static uint64_t blink_now_ms(void) {
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

static uint64_t half_period_ms(unsigned int hz, int double_speed) {
    uint64_t effective_hz = (uint64_t)(hz > 0u ? hz : 1u);
    uint64_t hp;
    if (double_speed) {
        effective_hz *= 2u;
    }
    hp = 500u / effective_hz;
    return hp > 0u ? hp : 1u;
}

static blink_pn_data *find_data(const rx_pn_net *net) {
    size_t i;
    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {
        if (s_data[i].in_use && s_data[i].net == net) {
            return &s_data[i];
        }
    }
    return NULL;
}

static blink_pn_data *find_or_alloc(rx_pn_net *net) {
    size_t i;
    blink_pn_data *data = find_data(net);
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

static void blink_pn_latch_inputs(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;
    int is_blinking;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->now_ms = blink_now_ms();
    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    if (data->latched_event) {
        data->net->places[P_REQUEST]++;
    }

    /* TOGGLE_DUE is a signal place: set fresh each tick. */
    is_blinking = (data->net->places[P_X1] > 0 || data->net->places[P_X2] > 0);
    data->net->places[P_TOGGLE_DUE] =
        (is_blinking &&
         data->next_toggle_ms > 0u &&
         data->now_ms >= data->next_toggle_ms) ? 1 : 0;
}

static void blink_pn_dump_outputs(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->output_enabled);
    if (data->latched_event) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

/* ------------------------------------------------------------------ */
/* Deferred actions                                                     */
/* ------------------------------------------------------------------ */

static void action_enter_x1(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;
    (void)ctx;
    if (data == NULL) return;
    data->output_enabled = 1;
    data->next_toggle_ms = data->now_ms + half_period_ms(data->base_hz, 0);
}

static void action_enter_x2(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;
    (void)ctx;
    if (data == NULL) return;
    data->output_enabled = 1;
    data->next_toggle_ms = data->now_ms + half_period_ms(data->base_hz, 1);
}

static void action_enter_off(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;
    (void)ctx;
    if (data == NULL) return;
    data->output_enabled = 0;
    data->next_toggle_ms = 0u;
}

static void action_do_toggle(rx_pn_context *ctx, void *user) {
    blink_pn_data *data = (blink_pn_data *)user;
    int in_x2;
    uint64_t hp;

    (void)ctx;
    if (data == NULL || data->net == NULL) {
        return;
    }

    data->output_enabled = !data->output_enabled;

    /* Determine speed from committed places (available after commit). */
    in_x2 = data->net->places[P_X2] > 0;
    hp = half_period_ms(data->base_hz, in_x2);

    if (data->next_toggle_ms == 0u) {
        data->next_toggle_ms = data->now_ms + hp;
    } else {
        data->next_toggle_ms += hp;
    }
}

/* ------------------------------------------------------------------ */
/* Transition table                                                     */
/* ------------------------------------------------------------------ */

/*
 *   T_TO_X1       — off + request   → x1     (start blinking at X1)
 *   T_TO_X2       — x1  + request   → x2     (speed up to X2)
 *   T_TO_OFF      — x2  + request   → off    (stop)
 *   T_TOGGLE_X1   — x1  + toggle    → x1     (self-loop: toggle light)
 *   T_TOGGLE_X2   — x2  + toggle    → x2     (self-loop: toggle light)
 *
 * Priority order (greedy sequential): button transitions are declared
 * before toggle transitions so a button press that arrives in the same
 * tick as a toggle due always advances the blink state first.
 */
static const rx_pn_arc t_to_x1_consume[]     = {{P_OFF, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_to_x1_produce[]     = {{P_X1, 1}};
static const rx_pn_arc t_to_x2_consume[]     = {{P_X1, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_to_x2_produce[]     = {{P_X2, 1}};
static const rx_pn_arc t_to_off_consume[]    = {{P_X2, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_to_off_produce[]    = {{P_OFF, 1}};
static const rx_pn_arc t_toggle_x1_consume[] = {{P_X1, 1}, {P_TOGGLE_DUE, 1}};
static const rx_pn_arc t_toggle_x1_produce[] = {{P_X1, 1}};
static const rx_pn_arc t_toggle_x2_consume[] = {{P_X2, 1}, {P_TOGGLE_DUE, 1}};
static const rx_pn_arc t_toggle_x2_produce[] = {{P_X2, 1}};

static const rx_pn_transition transitions[] = {
    {t_to_x1_consume,     2, t_to_x1_produce,     1, NULL, action_enter_x1},
    {t_to_x2_consume,     2, t_to_x2_produce,     1, NULL, action_enter_x2},
    {t_to_off_consume,    2, t_to_off_produce,     1, NULL, action_enter_off},
    {t_toggle_x1_consume, 2, t_toggle_x1_produce,  1, NULL, action_do_toggle},
    {t_toggle_x2_consume, 2, t_toggle_x2_produce,  1, NULL, action_do_toggle},
};

static const int initial_places[] = {1, 0, 0, 0, 0};

int blink_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int base_hz
) {
    blink_pn_data *data;

    if (net == NULL || base_hz == 0u) {
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
    data->output_enabled = 0;
    data->base_hz = base_hz;
    data->now_ms = 0;
    data->next_toggle_ms = 0;

    if (rx_pn_net_init(net, "blink", initial_places, BLINK_PLACE_COUNT,
                       transitions, BLINK_TRANSITION_COUNT, data,
                       blink_pn_latch_inputs, blink_pn_dump_outputs) != 0) {
        data->in_use = 0;
        return -1;
    }

    return 0;
}

int blink_pn_set_base_hz(rx_pn_net *net, unsigned int base_hz) {
    blink_pn_data *data = find_data(net);

    if (data == NULL || base_hz == 0u) {
        return -1;
    }

    data->base_hz = base_hz;
    /* Reschedule next toggle if currently blinking. */
    if (net->places[P_X1] > 0 || net->places[P_X2] > 0) {
        int in_x2 = net->places[P_X2] > 0;
        data->next_toggle_ms = blink_now_ms() + half_period_ms(base_hz, in_x2);
    }
    return 0;
}

unsigned int blink_pn_get_base_hz(const rx_pn_net *net) {
    const blink_pn_data *data = find_data(net);
    return data != NULL ? data->base_hz : 0u;
}

int blink_pn_get_output_enabled(const rx_pn_net *net) {
    const blink_pn_data *data = find_data(net);
    return data != NULL ? data->output_enabled : 0;
}
