"""Light FSM — simple toggle.

One button, one light.  Button press toggles ON ↔ OFF.
Equivalent to ``c/examples/fsm/00-light/light_fsm.c``.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))

from rxnet.fsm import Machine, Transition
from rxnet.runtime import Context

# Import shared driver relative to the examples root.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import app_driver

LIGHT_STATE_OFF = 0
LIGHT_STATE_ON = 1


class _Data:
    __slots__ = ("button_gpio", "light_gpio", "latched_event", "event_consumed", "output_enabled")

    def __init__(self, button_gpio: int, light_gpio: int) -> None:
        self.button_gpio = button_gpio
        self.light_gpio = light_gpio
        self.latched_event = False
        self.event_consumed = False
        self.output_enabled = False


def _latch(ctx: Context, data: _Data) -> None:
    data.latched_event = app_driver.latch_button_event(data.button_gpio)
    data.event_consumed = False


def _button_pressed(ctx: Context, data: _Data) -> bool:
    return data.latched_event


def _light_on(ctx: Context, data: _Data) -> None:
    data.output_enabled = True
    data.event_consumed = True


def _light_off(ctx: Context, data: _Data) -> None:
    data.output_enabled = False
    data.event_consumed = True


def _dump(ctx: Context, data: _Data) -> None:
    app_driver.set_light(data.light_gpio, data.output_enabled)
    if data.event_consumed:
        app_driver.clear_button_event(data.button_gpio)


_TRANSITIONS = [
    Transition(LIGHT_STATE_OFF, LIGHT_STATE_ON, guard=_button_pressed, action=_light_on),
    Transition(LIGHT_STATE_ON, LIGHT_STATE_OFF, guard=_button_pressed, action=_light_off),
]


def create_light_fsm(button_gpio: int, light_gpio: int) -> Machine:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)
    data = _Data(button_gpio, light_gpio)
    return Machine(
        name="light",
        state=LIGHT_STATE_OFF,
        transitions=_TRANSITIONS,
        user=data,
        latch_inputs_cb=_latch,
        dump_outputs_cb=_dump,
    )
