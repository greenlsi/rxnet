"""PN 04-mix — cooperative multi-rate scheduler.

Same scenario as ``main.py`` but driven by ``CoopExecutive``.

``CoopExecutive`` differs from ``CyclicExecutive``:

- **Dynamic dispatch**: runs the runtime when its deadline passes rather
  than advancing a fixed slot table.
- **Overrun-tolerant**: deadline advances by one period regardless of
  actual execution time, preventing accumulated phase slippage.
- **Multi-runtime**: can drive several independent runtimes (useful when
  PN and FSM nodes cannot share a runtime).

All four nodes (light_a 10 ms, blink_b 10 ms, auto_c 20 ms, cli 10 ms)
are in a single runtime; ``CoopExecutive`` drives it at the base period
(10 ms).

Usage::

    cd python
    uv run examples/pn/04-mix/main_coop.py
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

from rxnet.coop import CoopExecutive
from rxnet.pn import Net, Runtime as PnRuntime
from rxnet.runtime import Context

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


def cmd_quit(line: str, user: object) -> None:
    print("bye")
    sys.exit(0)


def main() -> None:
    light_a = create_light_pn(BUTTON_A_GPIO, LIGHT_A_GPIO)
    blink_b = create_blink_pn(BUTTON_A_GPIO, LIGHT_B_GPIO, DEFAULT_FREQ_B_HZ)
    auto_c  = create_auto_pn(BUTTON_B_GPIO, LIGHT_C_GPIO, DEFAULT_TIMEOUT_C_MS)

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

    cli.print_help()
    cmd_status("status", nets)
    cli.print_prompt()

    ce = CoopExecutive()
    ce.add(rt)
    ce.run()  # never returns


if __name__ == "__main__":
    main()
