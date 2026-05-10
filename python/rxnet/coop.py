# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

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
from typing import Any, TextIO

from .cyclic import sleep_until
from .runtime import (
    SCHED_ERROR,
    SCHED_SCHEDULABLE,
    SCHED_UNSCHEDULABLE,
    SchedReport,
    SchedResourceAccess,
    SchedTaskResult,
    effective_deadline,
)


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
        self._sched_check_enabled = False
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

    def enable_sched_check(self, enabled: bool = True) -> None:
        self._sched_check_enabled = bool(enabled)

    def _ordered_tasks(self) -> list[tuple[Any, int]]:
        tasks: list[tuple[Any, int]] = []
        for rt in self._runtimes:
            for idx, entry in enumerate(rt._entries):
                if entry.period_us <= 0:
                    continue
                tasks.append((rt, idx))
        tasks.sort(key=lambda t: effective_deadline(t[0]._entries[t[1]]))
        return tasks

    def check_schedulability(
        self,
        report: SchedReport | None = None,
        log: TextIO | None = None,
    ) -> int:
        tasks = self._ordered_tasks()
        rep = report if report is not None else SchedReport()
        rep.schedulable = True
        rep.unsupported = False
        rep.tasks.clear()

        for rt, idx in tasks:
            if rt._entries[idx].wcet_us <= 0:
                if log is not None:
                    print(f"coop: missing WCET for node {idx}", file=log)
                return SCHED_ERROR

        all_schedulable = True
        for i, (rt, idx) in enumerate(tasks):
            entry = rt._entries[idx]
            ci = entry.wcet_us
            di = effective_deadline(entry)
            lower = [tasks[j] for j in range(i + 1, len(tasks))]
            blocking = max((lrt._entries[lidx].wcet_us for lrt, lidx in lower), default=0)
            ri_prev = ci + blocking
            converged = False
            while True:
                interference = 0
                for hrt, hidx in tasks[:i]:
                    higher = hrt._entries[hidx]
                    jobs = (ri_prev + higher.period_us - 1) // higher.period_us
                    interference += jobs * higher.wcet_us
                ri = ci + interference + blocking
                if ri == ri_prev:
                    converged = True
                    break
                if ri > di:
                    break
                ri_prev = ri
            ok = converged and ri <= di
            all_schedulable = all_schedulable and ok
            rep.tasks.append(
                SchedTaskResult(
                    runtime=rt,
                    node_idx=idx,
                    period_us=entry.period_us,
                    deadline_us=di,
                    wcet_us=ci,
                    interference_us=ri - ci - blocking,
                    blocking_us=blocking,
                    response_us=ri,
                    schedulable=ok,
                    resource_accesses=[
                        SchedResourceAccess(resource_id=res, max_us=max_us)
                        for res, max_us in sorted((entry.resource_access_us or {}).items())
                    ],
                )
            )
            if log is not None:
                print(
                    f"node={idx} C={ci} T={entry.period_us} D={di} "
                    f"B={blocking} I={ri - ci - blocking} R={ri} {'OK' if ok else 'MISS'}",
                    file=log,
                )
        rep.schedulable = all_schedulable
        return SCHED_SCHEDULABLE if all_schedulable else SCHED_UNSCHEDULABLE

    def run(self) -> None:
        """Enter the cooperative scheduler loop until :meth:`stop` is requested."""
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        # Initialise all deadlines to now so every runtime fires on the
        # first pass (same as C's rx_coop_exec_run initialisation).
        if self._sched_check_enabled and self.check_schedulability() == SCHED_UNSCHEDULABLE:
            return

        now = time.monotonic()
        next_tick = {
            (id(rt), idx): now
            for rt in self._runtimes
            for idx, entry in enumerate(rt._entries)
            if entry.period_us > 0
        }
        next_activation_us = {key: 0 for key in next_tick}

        self._stop_requested = False
        try:
            while not self._stop_requested:
                now = time.monotonic()

                for rt in self._runtimes:
                    active: list[int] = []
                    activation_us = 0
                    have_activation = False
                    for idx, entry in enumerate(rt._entries):
                        key = (id(rt), idx)
                        if entry.period_us > 0 and now >= next_tick[key]:
                            active.append(idx)
                            node_activation_us = next_activation_us[key]
                            if not have_activation or node_activation_us < activation_us:
                                activation_us = node_activation_us
                                have_activation = True
                            next_tick[key] += entry.period_us * 1e-6
                            next_activation_us[key] += entry.period_us
                    if active:
                        active.sort(key=lambda idx: effective_deadline(rt._entries[idx]))
                        active.extend(
                            idx for idx, entry in enumerate(rt._entries) if entry.period_us == 0
                        )
                        rt.tick_nodes_at(active, activation_us)
                        if self._stop_requested:
                            break

                nearest = min(next_tick.values())
                if not self._stop_requested:
                    sleep_until(nearest)
        finally:
            if self._on_stop is not None:
                self._on_stop()
