// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GENERATED — do not edit by hand.
// Source: blink.yaml   (regenerate with: python -m rxnet.tools.gen blink.yaml)

#pragma once
#ifndef RXNET_BLINK_FSM_MODEL_H
#define RXNET_BLINK_FSM_MODEL_H

#include "rxnet/fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── states ──────────────────────────────────────────────────────────────── */
enum {
    BLINK_STATE_OFF = 0,
    BLINK_STATE_X1 = 1,
    BLINK_STATE_X2 = 2,
};

/* ── public factory ─────────────────────────────────────────────────────── */
/*
 * Initialise *machine as a ``blink`` FSM.
 * Call once; the machine must remain alive for the runtime's lifetime.
 */
void blink_fsm_create(rx_fsm_machine *machine, int button_gpio, int light_gpio, int base_hz);

/* ── expected callbacks (implement in blink_fsm.c) ──────────────────────── */
/*
 * static int button_pressed(const rx_fsm_context *ctx, void *user);
 * static int toggle_due(const rx_fsm_context *ctx, void *user);
 * static void enter_x1(rx_fsm_context *ctx, void *user);
 * static void enter_x2(rx_fsm_context *ctx, void *user);
 * static void enter_off(rx_fsm_context *ctx, void *user);
 * static void toggle_light(rx_fsm_context *ctx, void *user);
 */

#ifdef __cplusplus
}
#endif

#endif /* RXNET_BLINK_FSM_MODEL_H */
