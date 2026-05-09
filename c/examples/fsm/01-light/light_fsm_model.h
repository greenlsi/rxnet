// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GENERATED — do not edit by hand.
// Source: light.yaml   (regenerate with: python -m rxnet.tools.gen light.yaml)

#pragma once
#ifndef RXNET_LIGHT_FSM_MODEL_H
#define RXNET_LIGHT_FSM_MODEL_H

#include "rxnet/fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── states ──────────────────────────────────────────────────────────────── */
enum {
    LIGHT_STATE_OFF = 0,
    LIGHT_STATE_ON = 1,
};

/* ── public factory ─────────────────────────────────────────────────────── */
/*
 * Initialise *machine as a ``light`` FSM.
 * Call once; the machine must remain alive for the runtime's lifetime.
 */
void light_fsm_create(rx_fsm_machine *machine, int button_gpio, int light_gpio);

/* ── expected callbacks (implement in light_fsm.c) ──────────────────────── */
/*
 * static int button_pressed(const rx_fsm_context *ctx, void *user);
 * static void light_on(rx_fsm_context *ctx, void *user);
 * static void light_off(rx_fsm_context *ctx, void *user);
 */

#ifdef __cplusplus
}
#endif

#endif /* RXNET_LIGHT_FSM_MODEL_H */
