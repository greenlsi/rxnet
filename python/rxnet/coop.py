# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""rxnet.coop — Cooperative multi-rate scheduler.

Each registered runtime runs when its deadline passes.  After executing,
the deadline advances by the runtime's base period — this tracks drift
rather than resetting from now, preventing accumulated phase slippage.

Unlike the cyclic executive (static dispatch table), the cooperative
scheduler dynamically picks whichever runtime is due, making it more
resilient to occasional overruns.

Multiple runtimes with different periods can be registered.  The
scheduler sleeps until the nearest upcoming deadline across all runtimes.

Usage::

    from rxnet.coop import CoopExecutive

    ce = CoopExecutive()
    ce.add(pn_rt)    # PN nodes — base period from pn_rt.period_us
    ce.add(cli_rt)   # FSM cli — base period from cli_rt.period_us
    ce.run()         # returns when ce.stop() is requested

Single runtime with multi-rate nodes::

    rt = fsm.Runtime()
    rt.add_machine(light_a, 10_000)
    rt.add_machine(auto_c,  20_000)

    ce = CoopExecutive()
    ce.add(rt)
    ce.run()
"""
from __future__ import annotations

import time
from collections.abc import Callable
from typing import Any

from .cyclic import sleep_until


class CoopExecutive:
    """Cooperative multi-rate scheduler.

    Differences from :class:`CyclicExecutive`:

    * **Dynamic dispatch**: the scheduler always runs the runtime with the
      earliest deadline, rather than advancing a fixed slot counter.
    * **Overrun-tolerant**: if a runtime overruns its period the deadline
      still advances by one period (not reset to now), preventing cascaded
      latency.
    * **Multiple runtimes**: any number of runtimes can be registered; each
      is driven independently.
    """

    def __init__(self, on_stop: Callable[[], None] | None = None) -> None:
        self._runtimes: list[Any] = []
        self._stop_requested = False
        self._on_stop = on_stop

    def stop(self) -> None:
        """Request the scheduler loop to stop at the next safe point."""
        self._stop_requested = True

    def on_stop(self, callback: Callable[[], None] | None) -> None:
        """Register an optional callback run once before ``run`` returns."""
        self._on_stop = callback

    def add(self, runtime: Any) -> None:
        """Register *runtime*.  Its base period must be > 0."""
        if not runtime._built:
            runtime.build()
        if runtime.period_us == 0:
            raise ValueError(
                "runtime has no periodic nodes; "
                "register at least one node with period_us > 0"
            )
        self._runtimes.append(runtime)

    def run(self) -> None:
        """Enter the cooperative scheduler loop until :meth:`stop` is requested."""
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        # Initialise all deadlines to now so every runtime fires on the
        # first pass (same as C's rx_coop_exec_run initialisation).
        now = time.monotonic()
        deadlines = {id(rt): now for rt in self._runtimes}

        self._stop_requested = False
        try:
            while not self._stop_requested:
                now = time.monotonic()

                # Run every runtime whose deadline has passed.
                for rt in self._runtimes:
                    key = id(rt)
                    if now >= deadlines[key]:
                        rt.tick()
                        # Advance by one period (drift-tracking, not reset).
                        deadlines[key] += rt.period_us * 1e-6
                        if self._stop_requested:
                            break

                # Sleep until the nearest upcoming deadline.
                nearest = min(deadlines.values())
                if not self._stop_requested:
                    sleep_until(nearest)
        finally:
            if self._on_stop is not None:
                self._on_stop()
