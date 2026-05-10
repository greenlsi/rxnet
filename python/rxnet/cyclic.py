# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet.cyclic — Cyclic executive with hyperperiod dispatch table.

A cyclic executive runs runtimes at fixed periods using a static dispatch
table computed once at startup:

    base  = GCD of all runtime base periods  (= minimum common divisor)
    hyper = LCM of all runtime base periods  (= minimum common multiple)
    n_slots = hyper // base

Runtime *r* is scheduled at outer slot *s* when
``(s * base_us) % r.period_us == 0``.

The executive owns the activation table and invokes explicit runtime node
groups.  The runtime itself does not materialise a hyperperiod table.

Usage::

    from rxnet.cyclic import CyclicExecutive

    rt = fsm.Runtime()
    rt.add_machine(light,  10_000)   # 10 ms
    rt.add_machine(auto_c, 20_000)   # 20 ms
    rt.add_machine(cli,    10_000)   # 10 ms

    ce = CyclicExecutive()
    ce.add(rt)
    ce.run()                  # returns when ce.stop() is requested

Multiple runtimes with different base periods can be registered::

    ce.add(pn_rt)    # base period from pn_rt.period_us
    ce.add(cli_rt)   # base period from cli_rt.period_us
    ce.run()
"""
from __future__ import annotations

import math
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any, TextIO

from .runtime import (
    SCHED_ERROR,
    SCHED_SCHEDULABLE,
    SCHED_UNSCHEDULABLE,
    SchedReport,
    SchedResourceAccess,
    SchedTaskResult,
    effective_deadline,
)


def sleep_until(target: float) -> None:
    """Sleep until *target* as measured by ``time.monotonic()``.

    Handles spurious wakeups and slightly overshooting the target.
    """
    while True:
        delay = target - time.monotonic()
        if delay <= 0:
            return
        time.sleep(delay)


@dataclass(slots=True)
class _Activation:
    runtime: Any
    node_idx: int


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

    def __init__(self, on_stop: Callable[[], None] | None = None) -> None:
        self._runtimes: list[Any] = []  # Runtime objects
        self._slots: list[list[_Activation]] = []
        self._base_us = 0
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

    def enable_sched_check(self, enabled: bool = True) -> None:
        self._sched_check_enabled = bool(enabled)

    def _build_slots(self) -> None:
        periods: list[int] = []
        for rt in self._runtimes:
            for entry in rt._entries:
                if entry.period_us > 0:
                    periods.append(entry.period_us)
        if not periods:
            raise ValueError("no periodic nodes")

        base_us = math.gcd(*periods)
        hyper_us = math.lcm(*periods)
        slots: list[list[_Activation]] = []
        for s in range(hyper_us // base_us):
            t_us = s * base_us
            active: list[_Activation] = []
            for rt in self._runtimes:
                has_periodic = False
                for idx, entry in enumerate(rt._entries):
                    if entry.period_us > 0 and t_us % entry.period_us == 0:
                        active.append(_Activation(rt, idx))
                        has_periodic = True
                if has_periodic:
                    for idx, entry in enumerate(rt._entries):
                        if entry.period_us == 0:
                            active.append(_Activation(rt, idx))
            active.sort(key=lambda a: effective_deadline(a.runtime._entries[a.node_idx]))
            slots.append(active)
        self._base_us = base_us
        self._slots = slots

    def check_schedulability(
        self,
        report: SchedReport | None = None,
        log: TextIO | None = None,
    ) -> int:
        if not self._slots:
            self._build_slots()
        rep = report if report is not None else SchedReport()
        rep.schedulable = True
        rep.unsupported = False
        rep.tasks.clear()
        all_schedulable = True
        for slot_idx, slot in enumerate(self._slots):
            elapsed = 0
            slot_start = slot_idx * self._base_us
            for activation in slot:
                entry = activation.runtime._entries[activation.node_idx]
                if entry.wcet_us <= 0:
                    if log is not None:
                        print(f"cyclic: missing WCET for node {activation.node_idx}", file=log)
                    return SCHED_ERROR
                deadline = effective_deadline(entry)
                finish = elapsed + entry.wcet_us
                ok = finish <= deadline
                all_schedulable = all_schedulable and ok
                rep.tasks.append(
                    SchedTaskResult(
                        runtime=activation.runtime,
                        node_idx=activation.node_idx,
                        period_us=entry.period_us,
                        deadline_us=deadline,
                        wcet_us=entry.wcet_us,
                        response_us=finish,
                        schedulable=ok,
                        resource_accesses=[
                            SchedResourceAccess(resource_id=res, max_us=max_us)
                            for res, max_us in sorted((entry.resource_access_us or {}).items())
                        ],
                    )
                )
                if log is not None:
                    print(
                        f"slot={slot_idx} t={slot_start} node={activation.node_idx} "
                        f"C={entry.wcet_us} D={deadline} start={elapsed} finish={finish} "
                        f"{'OK' if ok else 'MISS'}",
                        file=log,
                    )
                elapsed = finish
        rep.schedulable = all_schedulable
        return SCHED_SCHEDULABLE if all_schedulable else SCHED_UNSCHEDULABLE

    def run(self) -> None:
        """Build the outer dispatch table and enter the scheduler loop.

        Returns when :meth:`stop` is requested.
        """
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        self._build_slots()
        if self._sched_check_enabled and self.check_schedulability() == SCHED_UNSCHEDULABLE:
            return

        base_s     = self._base_us * 1e-6
        next_tick  = time.monotonic()
        slot       = 0
        activation_us = 0

        self._stop_requested = False
        try:
            while not self._stop_requested:
                for rt in self._runtimes:
                    active = [a.node_idx for a in self._slots[slot] if a.runtime is rt]
                    if active:
                        rt.tick_nodes_at(active, activation_us)
                    if self._stop_requested:
                        break

                slot = (slot + 1) % len(self._slots)
                activation_us += self._base_us
                next_tick += base_s
                if not self._stop_requested:
                    sleep_until(next_tick)
        finally:
            if self._on_stop is not None:
                self._on_stop()
