# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""Blink FSM — three-state blinking light.

States: OFF → X1 (blink at base_hz) → X2 (blink at 2×base_hz) → OFF.
Button press cycles through states.  A self-loop transition fires on each
half-period to toggle the physical output.
Equivalent to ``c/examples/fsm/02-blink/blink_fsm.c``.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.fsm import Machine, Transition
from rxnet.runtime import Context

import app_driver

BLINK_STATE_OFF = 0
BLINK_STATE_X1 = 1
BLINK_STATE_X2 = 2


class _Data:
    __slots__ = (
        "button_gpio", "light_gpio",
        "latched_event", "event_consumed", "output_enabled",
        "base_hz", "now_ms", "next_toggle_ms",
    )

    def __init__(self, button_gpio: int, light_gpio: int, base_hz: int) -> None:
        self.button_gpio = button_gpio
        self.light_gpio = light_gpio
        self.latched_event = False
        self.event_consumed = False
        self.output_enabled = False
        self.base_hz = base_hz
        self.now_ms = 0
        self.next_toggle_ms = 0


def _now_ms() -> int:
    return int(time.monotonic() * 1000)


def _half_period_ms(base_hz: int, double_speed: bool) -> int:
    hz = max(base_hz, 1)
    if double_speed:
        hz *= 2
    hp = 500 // hz
    return max(hp, 1)


def _latch(ctx: Context, data: _Data) -> None:
    data.now_ms = _now_ms()
    data.latched_event = app_driver.latch_button_event(data.button_gpio)
    data.event_consumed = False


def _button_pressed(ctx: Context, data: _Data) -> bool:
    return data.latched_event


def _toggle_due(ctx: Context, data: _Data) -> bool:
    return (
        data.next_toggle_ms > 0
        and data.now_ms >= data.next_toggle_ms
    )


def _enter_x1(ctx: Context, data: _Data) -> None:
    data.event_consumed = True
    data.output_enabled = True
    data.next_toggle_ms = data.now_ms + _half_period_ms(data.base_hz, False)


def _enter_x2(ctx: Context, data: _Data) -> None:
    data.event_consumed = True
    data.output_enabled = True
    data.next_toggle_ms = data.now_ms + _half_period_ms(data.base_hz, True)


def _enter_off(ctx: Context, data: _Data) -> None:
    data.event_consumed = True
    data.output_enabled = False
    data.next_toggle_ms = 0


def _toggle_light(ctx: Context, data: _Data) -> None:
    data.output_enabled = not data.output_enabled
    # Determine speed from the machine's committed state (available after commit,
    # because actions run as deferred actions after commit).
    # We peek at the machine via a closure — here data.machine is set after creation.
    machine: Machine = data._machine  # type: ignore[attr-defined]
    double_speed = machine.state == BLINK_STATE_X2
    hp = _half_period_ms(data.base_hz, double_speed)
    if data.next_toggle_ms == 0:
        data.next_toggle_ms = data.now_ms + hp
    else:
        data.next_toggle_ms += hp


def _dump(ctx: Context, data: _Data) -> None:
    app_driver.set_light(data.light_gpio, data.output_enabled)
    if data.event_consumed:
        app_driver.clear_button_event(data.button_gpio)


_TRANSITIONS = [
    Transition(BLINK_STATE_OFF, BLINK_STATE_X1, guard=_button_pressed, action=_enter_x1),
    Transition(BLINK_STATE_X1, BLINK_STATE_X2, guard=_button_pressed, action=_enter_x2),
    Transition(BLINK_STATE_X1, BLINK_STATE_X1, guard=_toggle_due, action=_toggle_light),
    Transition(BLINK_STATE_X2, BLINK_STATE_OFF, guard=_button_pressed, action=_enter_off),
    Transition(BLINK_STATE_X2, BLINK_STATE_X2, guard=_toggle_due, action=_toggle_light),
]


def create_blink_fsm(button_gpio: int, light_gpio: int, base_hz: int) -> Machine:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)
    data = _Data(button_gpio, light_gpio, base_hz)
    machine = Machine(
        name="blink",
        state=BLINK_STATE_OFF,
        transitions=_TRANSITIONS,
        user=data,
        latch_inputs_cb=_latch,
        dump_outputs_cb=_dump,
    )
    data._machine = machine  # type: ignore[attr-defined]
    return machine


def set_base_hz(machine: Machine, base_hz: int) -> bool:
    if not isinstance(machine.user, _Data) or base_hz <= 0:
        return False
    machine.user.base_hz = base_hz
    if machine.state != BLINK_STATE_OFF:
        double_speed = machine.state == BLINK_STATE_X2
        machine.user.next_toggle_ms = _now_ms() + _half_period_ms(base_hz, double_speed)
    return True


def get_base_hz(machine: Machine) -> int:
    if not isinstance(machine.user, _Data):
        return 0
    return machine.user.base_hz


def get_output_enabled(machine: Machine) -> bool:
    if not isinstance(machine.user, _Data):
        return False
    return machine.user.output_enabled
