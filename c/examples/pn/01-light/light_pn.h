// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#pragma once

#include "rxnet/pn.h"
#include "app_driver.h"

/*
 * Toggle-light Petri net.
 *
 * Places
 *   P_OFF     — token present when light is off
 *   P_ON      — token present when light is on
 *   P_REQUEST — one token per unconsumed button press (accumulates)
 *
 * Semantics
 *   Each button press enqueues a REQUEST token.  Each tick, the net
 *   consumes one REQUEST token and toggles the light.  Multiple presses
 *   queued between ticks are processed one per tick in declaration order.
 */
int light_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio
);
