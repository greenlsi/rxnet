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
    ce.run()         # never returns

Single runtime with multi-rate nodes::

    rt = fsm.Runtime()
    rt.add_machine(light_a, 10_000)
    rt.add_machine(auto_c,  20_000)

    ce = CoopExecutive()
    ce.add(rt)
    ce.run()
"""
from __future__ import annotations

from typing import Any

from .cyclic import sleep_until

import time


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

    def __init__(self) -> None:
        self._runtimes: list[Any] = []

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
        """Enter the cooperative scheduler loop.  Never returns."""
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        # Initialise all deadlines to now so every runtime fires on the
        # first pass (same as C's rx_coop_exec_run initialisation).
        now = time.monotonic()
        deadlines = {id(rt): now for rt in self._runtimes}

        while True:
            now = time.monotonic()

            # Run every runtime whose deadline has passed.
            for rt in self._runtimes:
                key = id(rt)
                if now >= deadlines[key]:
                    rt.tick()
                    # Advance by one period (drift-tracking, not reset).
                    deadlines[key] += rt.period_us * 1e-6

            # Sleep until the nearest upcoming deadline.
            nearest = min(deadlines.values())
            sleep_until(nearest)
