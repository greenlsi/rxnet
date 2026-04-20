# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""FSM 04-mix — cooperative multi-rate scheduler with Perfetto tracing.

Same scenario as ``main_coop.py`` plus ``Tracer`` attached before ``run()``.

- ``GET http://localhost:7777/trace`` downloads the live binary trace.
- Type ``trace`` at the prompt to save ``trace.bin`` locally.
- Open with Perfetto (https://ui.perfetto.dev) or convert to HTML::

    python -m rxnet.tools.trace trace.bin --report report.html --open
    python -m rxnet.tools.trace http://localhost:7777 --report report.html --open

Usage::

    cd python
    uv run examples/fsm/04-mix/main_coop_trace.py
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
examples_root = str(Path(__file__).resolve().parents[2])
sys.path.insert(0, examples_root)
sys.path.insert(0, str(Path(examples_root) / "fsm" / "01-light"))
sys.path.insert(0, str(Path(examples_root) / "fsm" / "02-auto"))
sys.path.insert(0, str(Path(examples_root) / "fsm" / "03-blink"))

from rxnet.coop import CoopExecutive
from rxnet.fsm import Machine, Runtime
from rxnet.runtime import Context
from rxnet.trace import Tracer

import app_driver
from cli import Cli
from light_fsm import LIGHT_STATE_ON, create_light_fsm
from auto_fsm import AUTO_STATE_ON, create_auto_fsm, get_auto_off_timeout_ms, set_auto_off_timeout_ms
from blink_fsm import (
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
DEFAULT_FREQ_B_HZ = 2
DEFAULT_TIMEOUT_C_MS = 9000

FAST_PERIOD_US = 10_000
SLOW_PERIOD_US = 20_000
CLI_PERIOD_US  = 10_000


class CliNode:
    def __init__(self, cli: Cli) -> None:
        self._cli = cli

    def latch_inputs(self, ctx: Context) -> None:
        pass

    def evaluate(self, ctx: Context) -> None:
        pass

    def commit(self, ctx: Context) -> None:
        pass

    def dump_outputs(self, ctx: Context) -> None:
        self._cli.tick()


def _light_state(state: int) -> str:
    return "ON" if state == LIGHT_STATE_ON else "OFF"


def _blink_state(state: int) -> str:
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
    light_a, blink_b, auto_c = machines
    print(
        f"A(light): state={_light_state(light_a.state)}"
        f" | B(blink): state={_blink_state(blink_b.state)} output={int(get_output_enabled(blink_b))}"
        f" | C(auto): state={_light_state(auto_c.state)}"
    )
    print(
        f"B freq(hz): base={get_base_hz(blink_b)} effective={_effective_hz(blink_b)}"
        f" | C auto-off(ms)={get_auto_off_timeout_ms(auto_c)}"
    )


def cmd_freq(line: str, machines: tuple[Machine, Machine, Machine]) -> None:
    _, blink_b, _ = machines
    parts = line.split()
    if len(parts) != 2:
        print("usage: freq <hz>")
        return
    try:
        freq_hz = int(parts[1])
    except ValueError:
        print("usage: freq <hz>")
        return
    if freq_hz <= 0:
        print("frequency must be > 0 hz")
        return
    if set_base_hz(blink_b, freq_hz):
        print(f"blink B base frequency set to {freq_hz} hz")
    else:
        print("failed to update blink frequency (B)")


def cmd_timeout(line: str, machines: tuple[Machine, Machine, Machine]) -> None:
    _, _, auto_c = machines
    parts = line.split()
    if len(parts) != 2:
        print("usage: timeout <ms>")
        return
    try:
        timeout_ms = int(parts[1])
    except ValueError:
        print("usage: timeout <ms>")
        return
    if timeout_ms <= 0:
        print("timeout must be > 0 ms")
        return
    if set_auto_off_timeout_ms(auto_c, timeout_ms):
        print(f"auto C timeout set to {timeout_ms} ms")
    else:
        print("failed to update auto timeout (C)")


def cmd_trace(line: str, tracer: Tracer) -> None:
    path = "trace.bin"
    tracer.export(path)
    print(f"trace saved → {path}")
    print("  open:    python -m rxnet.tools.trace trace.bin --report report.html --open")
    print("  or live: python -m rxnet.tools.trace http://localhost:7777 --report report.html --open")


def cmd_quit(line: str, user: object) -> None:
    print("bye")
    sys.exit(0)


def main() -> None:
    light_a = create_light_fsm(BUTTON_A_GPIO, LIGHT_A_GPIO)
    blink_b = create_blink_fsm(BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ)
    auto_c  = create_auto_fsm(BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS)

    # Human-readable state names appear in the Perfetto trace.
    light_a.state_names = {0: "OFF", 1: "ON"}
    blink_b.state_names = {0: "OFF", 1: "X1", 2: "X2"}
    auto_c.state_names  = {0: "OFF", 1: "ON"}

    cli = Cli()
    machines = (light_a, blink_b, auto_c)

    cli.register("a",        cmd_button_a)
    cli.register("press a",  cmd_button_a)
    cli.register("b",        cmd_button_b)
    cli.register("press b",  cmd_button_b)
    cli.register("status",   cmd_status,  machines)
    cli.register("freq",     cmd_freq,    machines)
    cli.register("timeout",  cmd_timeout, machines)
    cli.register("help",     lambda l, u: cli.print_help())
    cli.register("quit",     cmd_quit)
    cli.register("exit",     cmd_quit)
    cli.add_help_line("freq <hz>")
    cli.add_help_line("timeout <ms>")

    cli_node = CliNode(cli)

    rt = Runtime()
    rt.add_machine(light_a,  FAST_PERIOD_US)
    rt.add_machine(blink_b,  FAST_PERIOD_US)
    rt.add_machine(auto_c,   SLOW_PERIOD_US)
    rt.add_node(cli_node,    CLI_PERIOD_US)

    tracer = Tracer(max_events=8192, phases=True)
    tracer.attach(rt)
    tracer.serve(port=7777)

    cli.register("trace", cmd_trace, tracer)
    cli.add_help_line("trace")

    cli.print_help()
    print("trace server: http://localhost:7777/trace")
    cmd_status("status", machines)
    cli.print_prompt()

    ce = CoopExecutive()
    ce.add(rt)
    ce.run()  # never returns


if __name__ == "__main__":
    main()
