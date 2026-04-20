# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""FSM 03-blink — interactive CLI demo.

Three blinking lights (A, B, C).  A and B share button A; C uses button B.
Default base frequencies: A=1 Hz, B=2 Hz, C=3 Hz.

Usage::

    cd python
    uv run examples/fsm/03-blink/main.py
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
from blink_fsm import (
    BLINK_STATE_OFF,
    BLINK_STATE_X1,
    BLINK_STATE_X2,
    create_blink_fsm,
    get_base_hz,
    get_output_enabled,
    set_base_hz,
)

LIGHT_A_GPIO = 2
LIGHT_B_GPIO = 4
LIGHT_C_GPIO = 5
BUTTON_A_GPIO = 0
BUTTON_B_GPIO = 15
DEFAULT_FREQ_A_HZ = 1
DEFAULT_FREQ_B_HZ = 2
DEFAULT_FREQ_C_HZ = 3
PERIOD_S = 0.010


def _state_name(state: int) -> str:
    if state == BLINK_STATE_X1:
        return "BLINK_X1"
    if state == BLINK_STATE_X2:
        return "BLINK_X2"
    return "OFF"


def _effective_hz(machine: Machine) -> int:
    base = get_base_hz(machine)
    if machine.state == BLINK_STATE_X2:
        return base * 2
    if machine.state == BLINK_STATE_X1:
        return base
    return 0


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
        f" | outputs: A={int(get_output_enabled(a))} B={int(get_output_enabled(b))} C={int(get_output_enabled(c))}"
    )
    print(
        f"freq(hz): base A={get_base_hz(a)} B={get_base_hz(b)} C={get_base_hz(c)}"
        f" | effective A={_effective_hz(a)} B={_effective_hz(b)} C={_effective_hz(c)}"
    )


def cmd_freq(line: str, machines: tuple[Machine, Machine, Machine]) -> None:
    parts = line.split()
    if len(parts) != 3:
        print("usage: freq <a|b|c> <hz>")
        return
    _, id_char, hz_str = parts
    try:
        freq_hz = int(hz_str)
    except ValueError:
        print("usage: freq <a|b|c> <hz>")
        return
    if freq_hz <= 0:
        print("frequency must be > 0 hz")
        return
    machine = _machine_for_id(machines, id_char)
    if machine is None:
        print(f"invalid light '{id_char}' (use a, b or c)")
        return
    if set_base_hz(machine, freq_hz):
        print(f"light {id_char.lower()} base frequency set to {freq_hz} hz")
    else:
        print(f"failed to update frequency for light {id_char.lower()}")


def cmd_quit(line: str, user: object) -> None:
    print("bye")
    sys.exit(0)


def main() -> None:
    blink_a = create_blink_fsm(BUTTON_A_GPIO, LIGHT_A_GPIO, DEFAULT_FREQ_A_HZ)
    blink_b = create_blink_fsm(BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ)
    blink_c = create_blink_fsm(BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_FREQ_C_HZ)

    rt = Runtime()
    rt.add_machine(blink_a)
    rt.add_machine(blink_b)
    rt.add_machine(blink_c)

    machines = (blink_a, blink_b, blink_c)

    cli = Cli()
    cli.register("a", cmd_button_a)
    cli.register("press a", cmd_button_a)
    cli.register("b", cmd_button_b)
    cli.register("press b", cmd_button_b)
    cli.register("status", cmd_status, machines)
    cli.register("freq", cmd_freq, machines)
    cli.register("help", lambda l, u: cli.print_help())
    cli.register("quit", cmd_quit)
    cli.register("exit", cmd_quit)
    cli.add_help_line("freq <a|b|c> <hz>")

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
