"""Auto PN — button turns light on; auto-off after timeout.

Places: P_OFF, P_ON, P_REQUEST, P_AUTO_OFF_DUE.
P_AUTO_OFF_DUE is a *signal place*: set to 1 or 0 each latch phase.
Equivalent to ``c/examples/pn/02-auto/auto_pn.c``.
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
P_ON = 1
P_REQUEST = 2
P_AUTO_OFF_DUE = 3


def _now_ms() -> int:
    return int(time.monotonic() * 1000)


def create_auto_pn(button_gpio: int, light_gpio: int, auto_off_timeout_ms: int) -> Net:
    app_driver.init_button(button_gpio)
    app_driver.init_light(light_gpio)

    state: dict[str, Any] = {
        "latched_event": False,
        "auto_off_timeout_ms": auto_off_timeout_ms,
        "wait_end_ms": 0,
        "wait_active": False,
    }

    def action_start_timer(ctx: Context, _user: Any) -> None:
        state["wait_end_ms"] = _now_ms() + state["auto_off_timeout_ms"]
        state["wait_active"] = True

    net = Net(
        name="auto",
        places=[1, 0, 0, 0],
        transitions=[
            # T_TURN_ON  — off + request  → on  (starts timer)
            Transition(
                consume=[Arc(P_OFF, 1), Arc(P_REQUEST, 1)],
                produce=[Arc(P_ON, 1)],
                action=action_start_timer,
            ),
            # T_REFRESH  — on  + request  → on  (resets timer)
            Transition(
                consume=[Arc(P_ON, 1), Arc(P_REQUEST, 1)],
                produce=[Arc(P_ON, 1)],
                action=action_start_timer,
            ),
            # T_AUTO_OFF — on  + due      → off
            Transition(
                consume=[Arc(P_ON, 1), Arc(P_AUTO_OFF_DUE, 1)],
                produce=[Arc(P_OFF, 1)],
            ),
        ],
    )

    def latch_cb(ctx: Context, _user: Any) -> None:
        now = _now_ms()
        state["latched_event"] = app_driver.latch_button_event(button_gpio)
        if state["latched_event"]:
            net.places[P_REQUEST] += 1

        # P_AUTO_OFF_DUE is a signal place: recompute each latch.
        due = (
            net.places[P_ON] > 0
            and state["wait_active"]
            and state["auto_off_timeout_ms"] > 0
            and now >= state["wait_end_ms"]
        )
        net.places[P_AUTO_OFF_DUE] = 1 if due else 0

    def dump_cb(ctx: Context, _user: Any) -> None:
        app_driver.set_light(light_gpio, net.places[P_ON] > 0)
        if state["latched_event"]:
            app_driver.clear_button_event(button_gpio)

    net.latch_inputs_cb = latch_cb
    net.dump_outputs_cb = dump_cb
    net.user = state
    return net


def set_timeout_ms(net: Net, timeout_ms: int) -> bool:
    if not isinstance(net.user, dict) or timeout_ms <= 0:
        return False
    net.user["auto_off_timeout_ms"] = timeout_ms
    if net.places[P_ON] > 0:
        net.user["wait_end_ms"] = _now_ms() + timeout_ms
        net.user["wait_active"] = True
    return True


def get_timeout_ms(net: Net) -> int:
    if not isinstance(net.user, dict):
        return 0
    return net.user.get("auto_off_timeout_ms", 0)
