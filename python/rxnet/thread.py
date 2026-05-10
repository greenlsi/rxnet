# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet.thread — thread-per-node executor with BSP activation groups."""

from __future__ import annotations

import copy
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any, TextIO

from .cyclic import sleep_until
from .runtime import (
    Context,
    SCHED_ERROR,
    SCHED_SCHEDULABLE,
    SCHED_UNSCHEDULABLE,
    SchedReport,
    SchedResourceAccess,
    SchedTaskResult,
    effective_deadline,
)


@dataclass(slots=True)
class _ActivationGroup:
    activation_us: int
    count: int
    latched_inputs: Any
    eval_b: threading.Barrier
    commit_b: threading.Barrier
    done: int = 0


class _RuntimeGroup:
    def __init__(self, runtime: Any) -> None:
        self.runtime = runtime
        self.node_contexts = [Context(runtime.context.inputs) for _ in runtime._entries]
        for ctx in self.node_contexts:
            ctx.bind_critical_section_state(runtime.context)
            ctx.set_critical_section_locking(True)
        self.active: dict[int, _ActivationGroup] = {}
        self.lock = threading.Lock()

    def active_count_at(self, activation_us: int) -> int:
        count = 0
        for entry in self.runtime._entries:
            if entry.period_us > 0 and activation_us % entry.period_us == 0:
                count += 1
        return count

    def get_activation_group(self, activation_us: int) -> _ActivationGroup:
        with self.lock:
            found = self.active.get(activation_us)
            if found is not None:
                return found
            count = self.active_count_at(activation_us)
            if count <= 0:
                raise RuntimeError("empty activation group")
            group = _ActivationGroup(
                activation_us=activation_us,
                count=count,
                latched_inputs=copy.copy(self.runtime.context.inputs),
                eval_b=threading.Barrier(count),
                commit_b=threading.Barrier(count),
            )
            self.active[activation_us] = group
            return group

    def finish_activation_group(self, group: _ActivationGroup) -> None:
        with self.lock:
            group.done += 1
            if group.done == group.count:
                self.active.pop(group.activation_us, None)


def _max_thread_blocking(tasks: list[tuple[Any, int]]) -> int:
    accesses = [
        (rt._entries[idx].resource_access_us or {})
        for rt, idx in tasks
    ]

    def best(task_pos: int, used_resources: set[int]) -> int:
        if task_pos >= len(accesses):
            return 0
        maximum = best(task_pos + 1, used_resources)
        for resource_id, elapsed_us in accesses[task_pos].items():
            if resource_id in used_resources:
                continue
            maximum = max(
                maximum,
                elapsed_us + best(task_pos + 1, used_resources | {resource_id}),
            )
        return maximum

    return best(0, set())


class ThreadExecutive:
    """BSP thread-per-node executor.

    Each periodic node runs in its own thread except the last node of the last
    runtime, which runs in the calling thread.  Each logical activation instant
    creates an activation group with two barriers: after evaluate and after
    commit plus deferred dispatch.  Async nodes are rejected.
    """

    def __init__(self, on_stop: Callable[[], None] | None = None) -> None:
        self._runtimes: list[Any] = []
        self._groups: list[_RuntimeGroup] = []
        self._stop_requested = threading.Event()
        self._on_stop = on_stop
        self._sched_check_enabled = False

    def stop(self) -> None:
        """Request all node loops to stop after their current tick."""
        self._stop_requested.set()

    def on_stop(self, callback: Callable[[], None] | None) -> None:
        """Register an optional callback run once before ``run`` returns."""
        self._on_stop = callback

    def add(self, runtime: Any) -> None:
        """Register *runtime*.  Async nodes are not supported."""
        if not runtime._built:
            runtime.build()
        if runtime.period_us == 0:
            raise ValueError(
                "runtime has no periodic nodes; "
                "register at least one node with period_us > 0"
            )
        for entry in runtime._entries:
            if entry.period_us <= 0:
                raise ValueError("async nodes are not supported by ThreadExecutive")
        self._runtimes.append(runtime)
        self._groups.append(_RuntimeGroup(runtime))

    def enable_sched_check(self, enabled: bool = True) -> None:
        self._sched_check_enabled = bool(enabled)

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
            entry = rt._entries[idx]
            if entry.period_us <= 0:
                rep.unsupported = True
                if log is not None:
                    print("thread: async node cannot be analysed", file=log)
                return SCHED_ERROR
            if entry.wcet_us <= 0:
                if log is not None:
                    print(f"thread: missing WCET for node {idx}", file=log)
                return SCHED_ERROR

        all_schedulable = True
        for i, (rt, idx) in enumerate(tasks):
            entry = rt._entries[idx]
            ci = entry.wcet_us
            di = effective_deadline(entry)
            lower = tasks[i + 1:]
            blocking = _max_thread_blocking(lower)
            ri_prev = ci + blocking
            converged = False
            while True:
                interference = 0
                for hrt, hidx in tasks[:i]:
                    higher = hrt._entries[hidx]
                    jobs = (ri_prev + higher.period_us - 1) // higher.period_us
                    interference += jobs * higher.wcet_us
                ri = ci + blocking + interference
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

    def _ordered_tasks(self) -> list[tuple[Any, int]]:
        tasks: list[tuple[Any, int]] = []
        for rt in self._runtimes:
            for idx, entry in enumerate(rt._entries):
                if entry.period_us > 0:
                    tasks.append((rt, idx))
        tasks.sort(key=lambda t: (effective_deadline(t[0]._entries[t[1]]), id(t[0]), t[1]))
        return tasks

    def run(self) -> None:
        """Spawn node threads and enter the main-thread node loop."""
        if not self._groups:
            raise RuntimeError("no runtimes registered")

        if self._sched_check_enabled:
            status = self.check_schedulability()
            if status == SCHED_UNSCHEDULABLE:
                return

        t0 = time.monotonic()
        threads: list[threading.Thread] = []
        main_loop_fn: Callable[[], None] | None = None
        last_group_idx = len(self._groups) - 1

        self._stop_requested.clear()

        for group_idx, group in enumerate(self._groups):
            runtime = group.runtime
            last_node_idx = len(runtime._entries) - 1
            for node_idx, entry in enumerate(runtime._entries):
                is_last = group_idx == last_group_idx and node_idx == last_node_idx

                def make_loop(
                    group: _RuntimeGroup = group,
                    node_idx: int = node_idx,
                    stop_requested: threading.Event = self._stop_requested,
                    period_us: int = entry.period_us,
                ) -> Callable[[], None]:
                    def loop() -> None:
                        runtime = group.runtime
                        entry = runtime._entries[node_idx]
                        node = entry.node
                        ctx = group.node_contexts[node_idx]
                        activation_us = 0
                        while True:
                            target = t0 + activation_us * 1e-6
                            sleep_until(target)
                            activation_group = group.get_activation_group(activation_us)
                            ctx.inputs = runtime.context.inputs
                            ctx.latched_inputs = activation_group.latched_inputs
                            ctx.activation_us = activation_group.activation_us

                            started = time.perf_counter_ns()
                            ctx.set_active_entry(entry)
                            node.latch_inputs(ctx)
                            node.evaluate(ctx)
                            try:
                                activation_group.eval_b.wait()
                            except threading.BrokenBarrierError:
                                return

                            node.commit(ctx)
                            ctx.dispatch_deferred(runtime._worker_pool)
                            try:
                                activation_group.commit_b.wait()
                            except threading.BrokenBarrierError:
                                return

                            node.dump_outputs(ctx)
                            ctx.set_active_entry(None)
                            elapsed_us = max(
                                1,
                                (time.perf_counter_ns() - started + 999) // 1000,
                            )
                            entry.wcet_us = max(entry.wcet_us, elapsed_us)
                            group.finish_activation_group(activation_group)

                            activation_us += period_us
                            if stop_requested.is_set():
                                return

                    return loop

                loop_fn = make_loop()
                if is_last:
                    main_loop_fn = loop_fn
                else:
                    thread = threading.Thread(target=loop_fn, daemon=True)
                    threads.append(thread)
                    thread.start()

        assert main_loop_fn is not None
        try:
            main_loop_fn()
        finally:
            self._stop_requested.set()
            for group in self._groups:
                with group.lock:
                    active = list(group.active.values())
                for activation_group in active:
                    activation_group.eval_b.abort()
                    activation_group.commit_b.abort()
            for thread in threads:
                thread.join(timeout=2.0)
            if self._on_stop is not None:
                self._on_stop()
