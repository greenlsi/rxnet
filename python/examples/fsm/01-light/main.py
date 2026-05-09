# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""FSM 01-light — interactive CLI demo.

Three lights (A, B, C).  A and B are controlled by button A.  C is
controlled by button B.  All use the simple toggle light_fsm.

Usage::

    cd python
    uv run examples/fsm/01-light/main.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

# Resolve library root and examples root.
sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.fsm import Machine, Runtime

import app_driver
from cli import Cli
from light_fsm import LIGHT_STATE_OFF, LIGHT_STATE_ON, create_light_fsm

LIGHT_A_GPIO = 2
LIGHT_B_GPIO = 4
LIGHT_C_GPIO = 5
BUTTON_A_GPIO = 0
BUTTON_B_GPIO = 15
PERIOD_S = 0.010


def _state_name(state: int) -> str:
    return "ON" if state == LIGHT_STATE_ON else "OFF"


def cmd_button_a(line: str, user: object) -> None:
    if app_driver.trigger_button(BUTTON_A_GPIO):
        print("button A queued")
    else:
        print("button A trigger failed")


def cmd_button_b(line: str, user: object) -> None:
    if app_driver.trigger_button(BUTTON_B_GPIO):
        print("button B queued")
    else:
        print("button B trigger failed")


def cmd_status(line: str, machines: tuple[Machine, Machine, Machine]) -> None:
    a, b, c = machines
    print(
        f"states: A={_state_name(a.state)} B={_state_name(b.state)} C={_state_name(c.state)}"
        f" | outputs: A={int(a.state != 0)} B={int(b.state != 0)} C={int(c.state != 0)}"
    )


def cmd_quit(line: str, user: object) -> None:
    print("bye")
    sys.exit(0)


def main() -> None:
    light_a = create_light_fsm(BUTTON_A_GPIO, LIGHT_A_GPIO)
    light_b = create_light_fsm(BUTTON_A_GPIO, LIGHT_B_GPIO)
    light_c = create_light_fsm(BUTTON_B_GPIO, LIGHT_C_GPIO)

    rt = Runtime()
    rt.add_machine(light_a)
    rt.add_machine(light_b)
    rt.add_machine(light_c)

    machines = (light_a, light_b, light_c)

    cli = Cli()
    cli.register("a", cmd_button_a)
    cli.register("press a", cmd_button_a)
    cli.register("b", cmd_button_b)
    cli.register("press b", cmd_button_b)
    cli.register("status", cmd_status, machines)
    cli.register("help", lambda l, u: cli.print_help())
    cli.register("quit", cmd_quit)
    cli.register("exit", cmd_quit)

    cli.print_help()
    cmd_status("status", machines)
    cli.print_prompt()

    next_tick = time.monotonic()
    while True:
        cli.tick()
        rt.tick()

        next_tick += PERIOD_S
        sleep_s = next_tick - time.monotonic()
        if sleep_s > 0:
            time.sleep(sleep_s)


if __name__ == "__main__":
    main()
