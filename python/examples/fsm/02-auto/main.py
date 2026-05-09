# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""FSM 02-auto — interactive CLI demo.

Three lights (A, B, C) with auto-off timers.
A and B share button A; C uses button B.
Default timeouts: A=3 s, B=6 s, C=9 s.

Usage::

    cd python
    uv run examples/fsm/02-auto/main.py
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.fsm import Machine, Runtime

import app_driver
from cli import Cli
from auto_fsm import (
    AUTO_STATE_OFF,
    AUTO_STATE_ON,
    create_auto_fsm,
    get_auto_off_timeout_ms,
    set_auto_off_timeout_ms,
)

LIGHT_A_GPIO = 2
LIGHT_B_GPIO = 4
LIGHT_C_GPIO = 5
BUTTON_A_GPIO = 0
BUTTON_B_GPIO = 15
DEFAULT_TIMEOUT_A_MS = 3000
DEFAULT_TIMEOUT_B_MS = 6000
DEFAULT_TIMEOUT_C_MS = 9000
PERIOD_S = 0.010


def _state_name(state: int) -> str:
    return "ON" if state == AUTO_STATE_ON else "OFF"


def _machine_for_id(machines: tuple[Machine, Machine, Machine], id_char: str) -> Machine | None:
    mapping = {"a": machines[0], "b": machines[1], "c": machines[2]}
    return mapping.get(id_char.lower())


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
    print(
        f"auto-off(ms): A={get_auto_off_timeout_ms(a)}"
        f" B={get_auto_off_timeout_ms(b)}"
        f" C={get_auto_off_timeout_ms(c)}"
    )


def cmd_timeout(line: str, machines: tuple[Machine, Machine, Machine]) -> None:
    parts = line.split()
    if len(parts) != 3:
        print("usage: timeout <a|b|c> <ms>")
        return
    _, id_char, ms_str = parts
    try:
        timeout_ms = int(ms_str)
    except ValueError:
        print("usage: timeout <a|b|c> <ms>")
        return
    if timeout_ms <= 0:
        print("timeout must be > 0 ms")
        return
    machine = _machine_for_id(machines, id_char)
    if machine is None:
        print(f"invalid light '{id_char}' (use a, b or c)")
        return
    if set_auto_off_timeout_ms(machine, timeout_ms):
        print(f"light {id_char.lower()} auto-off set to {timeout_ms} ms")
    else:
        print(f"failed to update timeout for light {id_char.lower()}")


def cmd_quit(line: str, user: object) -> None:
    print("bye")
    sys.exit(0)


def main() -> None:
    auto_a = create_auto_fsm(BUTTON_A_GPIO, LIGHT_A_GPIO, DEFAULT_TIMEOUT_A_MS)
    auto_b = create_auto_fsm(BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_TIMEOUT_B_MS)
    auto_c = create_auto_fsm(BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS)

    rt = Runtime()
    rt.add_machine(auto_a)
    rt.add_machine(auto_b)
    rt.add_machine(auto_c)

    machines = (auto_a, auto_b, auto_c)

    cli = Cli()
    cli.register("a", cmd_button_a)
    cli.register("press a", cmd_button_a)
    cli.register("b", cmd_button_b)
    cli.register("press b", cmd_button_b)
    cli.register("status", cmd_status, machines)
    cli.register("timeout", cmd_timeout, machines)
    cli.register("help", lambda l, u: cli.print_help())
    cli.register("quit", cmd_quit)
    cli.register("exit", cmd_quit)
    cli.add_help_line("timeout <a|b|c> <ms>")

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
