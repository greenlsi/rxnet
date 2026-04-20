// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#pragma once

#include "rxnet/pn.h"
#include "app_driver.h"

/*
 * Auto-off Petri net.
 *
 * Places
 *   P_OFF          — token present when light is off
 *   P_ON           — token present when light is on
 *   P_REQUEST      — one token per unconsumed button press (accumulates)
 *   P_AUTO_OFF_DUE — signal place; set to 1 in latch when the auto-off
 *                    timer has elapsed while the light is on, 0 otherwise
 *
 * Semantics
 *   Pressing the button while off turns the light on and starts the
 *   auto-off timer.  Pressing while on resets the timer.  When the timer
 *   expires the light turns off automatically.
 */
int auto_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int auto_off_timeout_ms
);

int auto_pn_set_timeout_ms(rx_pn_net *net, unsigned int timeout_ms);
unsigned int auto_pn_get_timeout_ms(const rx_pn_net *net);
