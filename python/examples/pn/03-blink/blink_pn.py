# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""Blink PN — three-state blinking light using a Petri net.

Places: P_OFF, P_X1, P_X2, P_REQUEST, P_TOGGLE_DUE.
P_TOGGLE_DUE is a *signal place*: set to 1 or 0 each latch phase.
Button transitions are declared before toggle transitions so a button
press always takes priority (greedy sequential semantics).
Equivalent to ``c/examples/pn/03-blink/blink_pn.c``.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.pn import Arc, Net, Transition
from rxnet.runtime import Context

import app_driver

P_OFF = 0
P_X1 = 1
P_X2 = 2
P_REQUEST = 3
P_TOGGLE_DUE = 4


def _now_ms() -> int:
    return int(time.monotonic() * 1000)


def _half_period_ms(base_hz: int, double_speed: bool) -> int:
    hz = max(base_hz, 1)
    if double_speed:
        hz *= 2
    hp = 500 // hz
    return max(hp, 1)


def create_blink_pn(button_gpio: int, light_gpio: int, base_hz: int) -> Net:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)

    state: dict[str, Any] = {
        "latched_event": False,
        "output_enabled": False,
        "base_hz": base_hz,
        "now_ms": 0,
        "next_toggle_ms": 0,
    }

    # ------------------------------------------------------------------ #
    # Deferred actions                                                     #
    # ------------------------------------------------------------------ #

    def action_enter_x1(ctx: Context, _user: Any) -> None:
        state["output_enabled"] = True
        state["next_toggle_ms"] = state["now_ms"] + _half_period_ms(state["base_hz"], False)

    def action_enter_x2(ctx: Context, _user: Any) -> None:
        state["output_enabled"] = True
        state["next_toggle_ms"] = state["now_ms"] + _half_period_ms(state["base_hz"], True)

    def action_enter_off(ctx: Context, _user: Any) -> None:
        state["output_enabled"] = False
        state["next_toggle_ms"] = 0

    def action_do_toggle(ctx: Context, _user: Any) -> None:
        state["output_enabled"] = not state["output_enabled"]
        in_x2 = net.places[P_X2] > 0  # committed places available here
        hp = _half_period_ms(state["base_hz"], in_x2)
        if state["next_toggle_ms"] == 0:
            state["next_toggle_ms"] = state["now_ms"] + hp
        else:
            state["next_toggle_ms"] += hp

    # ------------------------------------------------------------------ #
    # Net (button transitions declared before toggle for priority)        #
    # ------------------------------------------------------------------ #

    net = Net(
        name="blink",
        places=[1, 0, 0, 0, 0],
        transitions=[
            # T_TO_X1     — off + request   → x1
            Transition(consume=[Arc(P_OFF, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_X1, 1)], action=action_enter_x1),
            # T_TO_X2     — x1  + request   → x2
            Transition(consume=[Arc(P_X1, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_X2, 1)], action=action_enter_x2),
            # T_TO_OFF    — x2  + request   → off
            Transition(consume=[Arc(P_X2, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_OFF, 1)], action=action_enter_off),
            # T_TOGGLE_X1 — x1  + toggle    → x1  (self-loop)
            Transition(consume=[Arc(P_X1, 1), Arc(P_TOGGLE_DUE, 1)], produce=[Arc(P_X1, 1)], action=action_do_toggle),
            # T_TOGGLE_X2 — x2  + toggle    → x2  (self-loop)
            Transition(consume=[Arc(P_X2, 1), Arc(P_TOGGLE_DUE, 1)], produce=[Arc(P_X2, 1)], action=action_do_toggle),
        ],
    )

    # ------------------------------------------------------------------ #
    # Phase callbacks                                                      #
    # ------------------------------------------------------------------ #

    def latch_cb(ctx: Context, _user: Any) -> None:
        state["now_ms"] = _now_ms()
        state["latched_event"] = app_driver.latch_button_event(button_gpio)
        if state["latched_event"]:
            net.places[P_REQUEST] += 1

        # P_TOGGLE_DUE is a signal place: recompute each latch.
        is_blinking = net.places[P_X1] > 0 or net.places[P_X2] > 0
        due = (
            is_blinking
            and state["next_toggle_ms"] > 0
            and state["now_ms"] >= state["next_toggle_ms"]
        )
        net.places[P_TOGGLE_DUE] = 1 if due else 0

    def dump_cb(ctx: Context, _user: Any) -> None:
        app_driver.set_light(light_gpio, state["output_enabled"])
        if state["latched_event"]:
            app_driver.clear_button_event(button_gpio)

    net.latch_inputs_cb = latch_cb
    net.dump_outputs_cb = dump_cb
    net.user = state
    return net


def set_base_hz(net: Net, base_hz: int) -> bool:
    if not isinstance(net.user, dict) or base_hz <= 0:
        return False
    net.user["base_hz"] = base_hz
    if net.places[P_X1] > 0 or net.places[P_X2] > 0:
        in_x2 = net.places[P_X2] > 0
        net.user["next_toggle_ms"] = _now_ms() + _half_period_ms(base_hz, in_x2)
    return True


def get_base_hz(net: Net) -> int:
    if not isinstance(net.user, dict):
        return 0
    return net.user.get("base_hz", 0)


def get_output_enabled(net: Net) -> bool:
    if not isinstance(net.user, dict):
        return False
    return net.user.get("output_enabled", False)
