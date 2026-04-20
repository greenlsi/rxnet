// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT
//
// GENERATED — do not edit by hand.
// Source: auto.yaml   (regenerate with: python -m rxnet.tools.gen auto.yaml)

#pragma once
#ifndef RXNET_AUTO_FSM_MODEL_H
#define RXNET_AUTO_FSM_MODEL_H

#include "rxnet/fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── states ──────────────────────────────────────────────────────────────── */
enum {
    AUTO_STATE_OFF = 0,
    AUTO_STATE_ON = 1,
};

/* ── public factory ─────────────────────────────────────────────────────── */
/*
 * Initialise *machine as a ``auto`` FSM.
 * Call once; the machine must remain alive for the runtime's lifetime.
 */
void auto_fsm_create(rx_fsm_machine *machine, int button_gpio, int light_gpio, unsigned int auto_off_timeout_ms);

/* ── expected callbacks (implement in auto_fsm.c) ──────────────────────── */
/*
 * static int button_pressed(const rx_fsm_context *ctx, void *user);
 * static int auto_off_elapsed(const rx_fsm_context *ctx, void *user);
 * static void auto_on(rx_fsm_context *ctx, void *user);
 * static void auto_off(rx_fsm_context *ctx, void *user);
 */

#ifdef __cplusplus
}
#endif

#endif /* RXNET_AUTO_FSM_MODEL_H */
