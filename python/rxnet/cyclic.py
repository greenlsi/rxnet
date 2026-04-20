# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""rxnet.cyclic — Cyclic executive with hyperperiod dispatch table.

A cyclic executive runs runtimes at fixed periods using a static dispatch
table computed once at startup:

    base  = GCD of all runtime base periods  (= minimum common divisor)
    hyper = LCM of all runtime base periods  (= minimum common multiple)
    n_slots = hyper // base

Runtime *r* is scheduled at outer slot *s* when
``(s * base_us) % r.period_us == 0``.

Each runtime manages its own internal slot counter.  ``runtime.tick()``
runs the active nodes for the current internal slot and advances the
slot counter automatically.

Usage::

    from rxnet.cyclic import CyclicExecutive

    rt = fsm.Runtime()
    rt.add_machine(light,  10_000)   # 10 ms
    rt.add_machine(auto_c, 20_000)   # 20 ms
    rt.add_machine(cli,    10_000)   # 10 ms

    ce = CyclicExecutive()
    ce.add(rt)
    ce.run()                  # never returns

Multiple runtimes with different base periods can be registered::

    ce.add(pn_rt)    # base period from pn_rt.period_us
    ce.add(cli_rt)   # base period from cli_rt.period_us
    ce.run()
"""
from __future__ import annotations

import math
import time
from typing import Any


def sleep_until(target: float) -> None:
    """Sleep until *target* as measured by ``time.monotonic()``.

    Handles spurious wakeups and slightly overshooting the target.
    """
    while True:
        delay = target - time.monotonic()
        if delay <= 0:
            return
        time.sleep(delay)


class CyclicExecutive:
    """Single-thread cyclic executive scheduler.

    Register runtimes with :meth:`add`, then call :meth:`run`.  The outer
    dispatch table is built automatically from the runtimes' base periods.

    Each runtime's internal hyperperiod table is built by the runtime
    itself (automatically on the first :meth:`Runtime.tick` call or
    explicitly via :meth:`Runtime.build`).

    The scheduler is static: if you need to change periods at runtime, use
    the cooperative scheduler (``CoopExecutive``) instead.
    """

    def __init__(self) -> None:
        self._runtimes: list[Any] = []  # Runtime objects

    def add(self, runtime: Any) -> None:
        """Register *runtime* to be driven by the cyclic executive.

        The runtime's base period (``runtime.period_us``) must be > 0.
        :meth:`build` is called on the runtime here if not already done.
        """
        if not runtime._built:
            runtime.build()
        if runtime.period_us == 0:
            raise ValueError(
                "runtime has no periodic nodes; "
                "register at least one node with period_us > 0"
            )
        self._runtimes.append(runtime)

    def run(self) -> None:
        """Build the outer dispatch table and enter the scheduler loop.

        Never returns.
        """
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        periods   = [rt.period_us for rt in self._runtimes]
        base_us   = math.gcd(*periods)
        hyper_us  = math.lcm(*periods)
        n_slots   = hyper_us // base_us

        # Build outer slot table: which runtimes fire at each outer slot.
        slots = [
            [rt for rt in self._runtimes if (s * base_us) % rt.period_us == 0]
            for s in range(n_slots)
        ]

        base_s     = base_us * 1e-6
        next_tick  = time.monotonic()
        slot       = 0

        while True:
            for rt in slots[slot]:
                rt.tick()

            slot = (slot + 1) % n_slots
            next_tick += base_s
            sleep_until(next_tick)
