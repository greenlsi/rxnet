#pragma once

#include "rxnet/pn.h"
#include "app_driver.h"

/*
 * Blinking Petri net.
 *
 * Places
 *   P_OFF        — token present when light is off
 *   P_X1         — token present when blinking at base_hz
 *   P_X2         — token present when blinking at 2 * base_hz
 *   P_REQUEST    — one token per unconsumed button press (accumulates)
 *   P_TOGGLE_DUE — signal place; set to 1 in latch when the toggle
 *                  timer has elapsed while blinking, 0 otherwise
 *
 * Semantics
 *   Button presses cycle: OFF → X1 → X2 → OFF.
 *   While in X1 or X2 the light toggles on each TOGGLE_DUE signal.
 *   Multiple presses queued between ticks advance the cycle one step
 *   per tick in declaration order (greedy sequential semantics).
 */
int blink_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int base_hz
);

int blink_pn_set_base_hz(rx_pn_net *net, unsigned int base_hz);
unsigned int blink_pn_get_base_hz(const rx_pn_net *net);
int blink_pn_get_output_enabled(const rx_pn_net *net);
