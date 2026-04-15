"""Light PN — simple toggle using a Petri net.

Places: P_OFF, P_ON, P_REQUEST.
Button press adds a token to P_REQUEST.  Transitions consume the request
and move the token between P_OFF and P_ON.
Equivalent to ``c/examples/pn/01-light/light_pn.c``.
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.pn import Arc, Net, Transition
from rxnet.runtime import Context

import app_driver

P_OFF = 0
P_ON = 1
P_REQUEST = 2

_TRANSITIONS = [
    Transition(consume=[Arc(P_OFF, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_ON, 1)]),
    Transition(consume=[Arc(P_ON, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_OFF, 1)]),
]


def create_light_pn(button_gpio: int, light_gpio: int) -> Net:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)

    net = Net(
        name="light",
        places=[1, 0, 0],
        transitions=_TRANSITIONS,
    )

    # Track latched event state alongside the net.
    state = {"latched_event": False}

    def latch_cb(ctx: Context, _user: Any) -> None:
        state["latched_event"] = app_driver.latch_button_event(button_gpio)
        if state["latched_event"]:
            net.places[P_REQUEST] += 1

    def dump_cb(ctx: Context, _user: Any) -> None:
        app_driver.set_light(light_gpio, net.places[P_ON] > 0)
        if state["latched_event"]:
            app_driver.clear_button_event(button_gpio)

    net.latch_inputs_cb = latch_cb
    net.dump_outputs_cb = dump_cb
    return net
