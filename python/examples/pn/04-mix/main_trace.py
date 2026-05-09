# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""PN 04-mix — cyclic executive with Perfetto tracing.

Same scenario as ``main.py`` plus ``Tracer`` attached before ``run()``.

- ``GET http://localhost:7777/trace`` downloads the live binary trace.
- Type ``trace`` at the prompt to save ``trace.bin`` locally.
- Open with Perfetto (https://ui.perfetto.dev) or convert to HTML::

    python -m rxnet.tools.trace trace.bin --report report.html --open
    python -m rxnet.tools.trace http://localhost:7777 --report report.html --open

Usage::

    cd python
    uv run examples/pn/04-mix/main_trace.py
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
examples_root = str(Path(__file__).resolve().parents[2])
sys.path.insert(0, examples_root)
sys.path.insert(0, str(Path(examples_root) / "pn" / "01-light"))
sys.path.insert(0, str(Path(examples_root) / "pn" / "02-auto"))
sys.path.insert(0, str(Path(examples_root) / "pn" / "03-blink"))

from rxnet.cyclic import CyclicExecutive
from rxnet.pn import Net, Runtime as PnRuntime
from rxnet.runtime import Context
from rxnet.trace import Tracer

import app_driver
from cli import Cli
from light_pn import P_ON as LIGHT_P_ON, create_light_pn
from auto_pn import P_ON as AUTO_P_ON, create_auto_pn, get_timeout_ms, set_timeout_ms
from blink_pn import P_X1, P_X2, create_blink_pn, get_base_hz, get_output_enabled, set_base_hz

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


# ------------------------------------------------------------------ #
# CLI node wrapper                                                     #
# ------------------------------------------------------------------ #

class CliNode:
    """Wraps Cli as a Runtime Node.  Dispatches one command per dump phase."""

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


# ------------------------------------------------------------------ #
# CLI command handlers                                                 #
# ------------------------------------------------------------------ #

def _light_state(net: Net, p_on: int) -> str:
    return "ON" if net.places[p_on] > 0 else "OFF"


def _blink_state(net: Net) -> str:
    if net.places[P_X2] > 0:
        return "X2"
    if net.places[P_X1] > 0:
        return "X1"
    return "OFF"


def _blink_effective_hz(net: Net) -> int:
    base = get_base_hz(net)
    if net.places[P_X2] > 0:
        return base * 2
    if net.places[P_X1] > 0:
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


def cmd_status(line: str, nets: tuple[Net, Net, Net]) -> None:
    light_a, blink_b, auto_c = nets
    print(
        f"A(light): {_light_state(light_a, LIGHT_P_ON)}"
        f" | B(blink): {_blink_state(blink_b)} output={int(get_output_enabled(blink_b))}"
        f" | C(auto): {_light_state(auto_c, AUTO_P_ON)}"
    )
    print(
        f"B base_hz={get_base_hz(blink_b)} effective={_blink_effective_hz(blink_b)}"
        f" | C auto-off={get_timeout_ms(auto_c)} ms"
    )


def cmd_freq(line: str, nets: tuple[Net, Net, Net]) -> None:
    _, blink_b, _ = nets
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


def cmd_timeout(line: str, nets: tuple[Net, Net, Net]) -> None:
    _, _, auto_c = nets
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
    if set_timeout_ms(auto_c, timeout_ms):
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


# ------------------------------------------------------------------ #
# Main                                                                 #
# ------------------------------------------------------------------ #

def main() -> None:
    light_a = create_light_pn(BUTTON_A_GPIO, LIGHT_A_GPIO)
    blink_b = create_blink_pn(BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ)
    auto_c  = create_auto_pn(BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS)

    # Human-readable place names appear in the Perfetto trace.
    light_a.place_names = {0: "OFF", 1: "ON", 2: "REQUEST"}
    blink_b.place_names = {0: "OFF", 1: "X1", 2: "X2", 3: "REQUEST", 4: "TOGGLE_DUE"}
    auto_c.place_names  = {0: "OFF", 1: "ON", 2: "REQUEST", 3: "AUTO_OFF_DUE"}

    cli = Cli()
    nets = (light_a, blink_b, auto_c)

    cli.register("a",        cmd_button_a)
    cli.register("press a",  cmd_button_a)
    cli.register("b",        cmd_button_b)
    cli.register("press b",  cmd_button_b)
    cli.register("status",   cmd_status,  nets)
    cli.register("freq",     cmd_freq,    nets)
    cli.register("timeout",  cmd_timeout, nets)
    cli.register("help",     lambda l, u: cli.print_help())
    cli.register("quit",     cmd_quit)
    cli.register("exit",     cmd_quit)
    cli.add_help_line("freq <hz>")
    cli.add_help_line("timeout <ms>")

    cli_node = CliNode(cli)

    rt = PnRuntime()
    rt.add_net(light_a,  FAST_PERIOD_US)
    rt.add_net(blink_b,  FAST_PERIOD_US)
    rt.add_net(auto_c,   SLOW_PERIOD_US)
    rt.add_node(cli_node, CLI_PERIOD_US)

    tracer = Tracer(max_events=8192, phases=True)
    tracer.attach(rt)
    tracer.serve(port=7777)

    cli.register("trace", cmd_trace, tracer)
    cli.add_help_line("trace")

    cli.print_help()
    print("trace server: http://localhost:7777/trace")
    cmd_status("status", nets)
    cli.print_prompt()

    ce = CyclicExecutive()
    ce.add(rt)
    ce.run()  # never returns


if __name__ == "__main__":
    main()
