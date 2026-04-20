# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""Auto FSM — button turns light on; it turns off automatically after a timeout.

Button press while ON resets the timer.
Equivalent to ``c/examples/fsm/01-auto/auto_fsm.c``.
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

AUTO_STATE_OFF = 0
AUTO_STATE_ON = 1


class _Data:
    __slots__ = (
        "button_gpio", "light_gpio",
        "latched_event", "event_consumed", "output_enabled",
        "auto_off_timeout_ms", "now_ms", "wait_end_ms", "wait_active",
    )

    def __init__(self, button_gpio: int, light_gpio: int, auto_off_timeout_ms: int) -> None:
        self.button_gpio = button_gpio
        self.light_gpio = light_gpio
        self.latched_event = False
        self.event_consumed = False
        self.output_enabled = False
        self.auto_off_timeout_ms = auto_off_timeout_ms
        self.now_ms = 0
        self.wait_end_ms = 0
        self.wait_active = False


def _now_ms() -> int:
    return int(time.monotonic() * 1000)


def _latch(ctx: Context, data: _Data) -> None:
    data.now_ms = _now_ms()
    data.latched_event = app_driver.latch_button_event(data.button_gpio)
    data.event_consumed = False

    if data.latched_event:
        data.wait_end_ms = data.now_ms + data.auto_off_timeout_ms
        data.wait_active = True

    # Reset timer when already off (belt-and-suspenders guard).
    if not data.output_enabled:
        data.wait_active = False


def _button_pressed(ctx: Context, data: _Data) -> bool:
    return data.latched_event


def _auto_off_elapsed(ctx: Context, data: _Data) -> bool:
    return (
        data.wait_active
        and data.auto_off_timeout_ms > 0
        and data.now_ms >= data.wait_end_ms
    )


def _auto_on(ctx: Context, data: _Data) -> None:
    data.output_enabled = True
    data.event_consumed = True
    if not data.wait_active:
        data.wait_end_ms = _now_ms() + data.auto_off_timeout_ms
        data.wait_active = True


def _auto_off(ctx: Context, data: _Data) -> None:
    data.output_enabled = False
    data.wait_active = False


def _dump(ctx: Context, data: _Data) -> None:
    app_driver.set_light(data.light_gpio, data.output_enabled)
    if data.event_consumed:
        app_driver.clear_button_event(data.button_gpio)


_TRANSITIONS = [
    Transition(AUTO_STATE_OFF, AUTO_STATE_ON, guard=_button_pressed, action=_auto_on),
    Transition(AUTO_STATE_ON, AUTO_STATE_ON, guard=_button_pressed, action=_auto_on),
    Transition(AUTO_STATE_ON, AUTO_STATE_OFF, guard=_auto_off_elapsed, action=_auto_off),
]


def create_auto_fsm(
    button_gpio: int,
    light_gpio: int,
    auto_off_timeout_ms: int,
) -> Machine:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)
    data = _Data(button_gpio, light_gpio, auto_off_timeout_ms)
    return Machine(
        name="auto",
        state=AUTO_STATE_OFF,
        transitions=_TRANSITIONS,
        user=data,
        latch_inputs_cb=_latch,
        dump_outputs_cb=_dump,
    )


def set_auto_off_timeout_ms(machine: Machine, timeout_ms: int) -> bool:
    if not isinstance(machine.user, _Data) or timeout_ms <= 0:
        return False
    machine.user.auto_off_timeout_ms = timeout_ms
    if machine.state == AUTO_STATE_ON:
        machine.user.wait_end_ms = _now_ms() + timeout_ms
        machine.user.wait_active = True
    return True


def get_auto_off_timeout_ms(machine: Machine) -> int:
    if not isinstance(machine.user, _Data):
        return 0
    return machine.user.auto_off_timeout_ms
