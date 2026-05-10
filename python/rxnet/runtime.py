# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import math
import threading
import time
from concurrent.futures import Executor
from dataclasses import dataclass
from typing import Any, Protocol, TextIO

from .worker_pool import Priority, WorkerPool


class Node(Protocol):
    def latch_inputs(self, ctx: Context) -> None: ...
    def evaluate(self, ctx: Context) -> None: ...
    def commit(self, ctx: Context) -> None: ...
    def dump_outputs(self, ctx: Context) -> None: ...


@dataclass(slots=True)
class DeferredAction:
    fn:       Any
    user:     Any
    priority: Priority = Priority.NORMAL


@dataclass(slots=True)
class _NodeEntry:
    node:      Any   # Node protocol
    period_us: int = 0  # 0 = async (active in every slot)
    deadline_us: int = 0  # 0 = same as period_us
    wcet_us: int = 0
    resource_access_us: dict[int, int] | None = None


SCHED_UNSCHEDULABLE = 0
SCHED_SCHEDULABLE = 1
SCHED_ERROR = -1
SCHED_UNSUPPORTED = -2


@dataclass(slots=True)
class SchedResourceAccess:
    resource_id: int
    max_us: int


@dataclass(slots=True)
class SchedTaskResult:
    runtime: Any
    node_idx: int
    period_us: int
    deadline_us: int
    wcet_us: int
    interference_us: int = 0
    blocking_us: int = 0
    response_us: int = 0
    schedulable: bool = True
    resource_accesses: list[SchedResourceAccess] | None = None

    def __post_init__(self) -> None:
        if self.resource_accesses is None:
            self.resource_accesses = []


@dataclass(slots=True)
class SchedReport:
    schedulable: bool = True
    unsupported: bool = False
    tasks: list[SchedTaskResult] | None = None

    def __post_init__(self) -> None:
        if self.tasks is None:
            self.tasks = []


def effective_deadline(entry: _NodeEntry) -> int:
    return entry.deadline_us if entry.deadline_us > 0 else entry.period_us


def sched_status_name(status: int) -> str:
    if status == SCHED_SCHEDULABLE:
        return "schedulable"
    if status == SCHED_UNSCHEDULABLE:
        return "not schedulable"
    if status == SCHED_UNSUPPORTED:
        return "unsupported"
    return "error"


def print_sched_report(
    name: str,
    status: int,
    report: SchedReport,
    out: TextIO | None = None,
) -> None:
    stream = out if out is not None else __import__("sys").stdout
    print(f"{name}: {sched_status_name(status)}", file=stream)
    for task in report.tasks or []:
        state = "OK" if task.schedulable else "MISS"
        print(
            f"  node={task.node_idx} T={task.period_us} D={task.deadline_us} "
            f"C={task.wcet_us} I={task.interference_us} B={task.blocking_us} "
            f"R={task.response_us} {state}",
            file=stream,
        )
        if task.resource_accesses:
            accesses = " ".join(
                f"resource={access.resource_id} max={access.max_us}us"
                for access in task.resource_accesses
            )
            print(f"    resources: {accesses}", file=stream)


class Context:
    """Shared state passed to every node callback within a tick.

    Thread safety
    -------------
    ``ctx.inputs`` may be written from any thread between ticks.
    ``tick()`` itself is **not** thread-safe: only one thread may call it
    at a time.  If a producer thread updates ``ctx.inputs`` while a
    consumer thread is inside ``tick()``, the caller must coordinate with
    an external lock.

    ``enqueue_deferred_action()`` **is** thread-safe: it may be called
    from parallel node phases (commit running in a thread pool) without
    additional synchronisation.

    Shallow-copy contract
    ---------------------
    ``latch_inputs()`` performs a *shallow* copy (``copy.copy``) of
    ``inputs``.  Values stored in ``ctx.inputs`` must therefore be either:

    * **immutable** scalars (``int``, ``bool``, ``float``, ``str``), or
    * **replaced atomically** (``ctx.inputs["key"] = new_value``) rather
      than mutated in-place.

    Mutating a mutable value after ``latch_inputs()`` has run will
    silently corrupt ``latched_inputs`` for that tick.
    """

    __slots__ = (
        "inputs",
        "latched_inputs",
        "activation_us",
        "_deferred_actions",
        "_deferred_lock",
        "_active",
        "_resource_locks",
        "_resource_lock_guard",
        "_resource_locking_enabled",
        "_clock_ns",
    )

    def __init__(self, inputs: Any = None) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)
        self.activation_us = 0
        self._deferred_actions: list[DeferredAction] = []
        self._deferred_lock = threading.Lock()
        self._active = threading.local()
        self._resource_locks: dict[int, threading.Lock] = {}
        self._resource_lock_guard = threading.Lock()
        self._resource_locking_enabled = False
        self._clock_ns = time.perf_counter_ns

    def latch_inputs(self) -> None:
        """Snapshot ``inputs`` into ``latched_inputs`` (shallow copy)."""
        self.latched_inputs = copy.copy(self.inputs)

    def enqueue_deferred_action(
        self,
        fn: Any,
        user: Any,
        priority: Priority = Priority.NORMAL,
    ) -> None:
        """Enqueue a deferred action. Thread-safe."""
        if fn is None:
            raise TypeError("fn must not be None")
        with self._deferred_lock:
            self._deferred_actions.append(
                DeferredAction(fn=fn, user=user, priority=priority)
            )

    def run_deferred_actions(self) -> None:
        """Run all deferred actions synchronously in priority order (FIFO within priority)."""
        with self._deferred_lock:
            actions = sorted(
                self._deferred_actions,
                key=lambda a: -int(a.priority),  # stable sort → FIFO within priority
            )
            self._deferred_actions.clear()
        for action in actions:
            action.fn(self, action.user)

    def dispatch_deferred(self, worker_pool: WorkerPool | None = None) -> None:
        """Dispatch all enqueued deferred actions.

        * ``worker_pool=None`` (default): run synchronously in priority order,
          blocking until all actions complete (current behaviour — deferred runs
          before dump).
        * ``worker_pool=<pool>``: post every action to the pool and return
          immediately.  Actions may run *after* dump; results arrive in
          ``ctx.inputs`` at the **next** latch.
        """
        with self._deferred_lock:
            actions = list(self._deferred_actions)
            self._deferred_actions.clear()
        if worker_pool is None:
            actions.sort(key=lambda a: -int(a.priority))
            for action in actions:
                action.fn(self, action.user)
        else:
            for action in actions:
                worker_pool.submit(action.fn, self, action.user, action.priority)

    def bind_critical_section_state(self, other: Context) -> None:
        self._resource_locks = other._resource_locks
        self._resource_lock_guard = other._resource_lock_guard
        self._resource_locking_enabled = other._resource_locking_enabled

    def set_critical_section_locking(self, enabled: bool) -> None:
        self._resource_locking_enabled = bool(enabled)

    def set_active_entry(self, entry: _NodeEntry | None) -> None:
        self._active.entry = entry

    def critical_section(self, resource_id: int) -> _CriticalSection:
        return _CriticalSection(self, int(resource_id))


class _CriticalSection:
    def __init__(self, ctx: Context, resource_id: int) -> None:
        self._ctx = ctx
        self._resource_id = resource_id
        self._lock: threading.Lock | None = None
        self._started_ns = 0

    def __enter__(self) -> _CriticalSection:
        active_resource = getattr(self._ctx._active, "resource_id", None)
        if active_resource is not None:
            raise RuntimeError("nested critical sections are not supported")
        if self._ctx._resource_locking_enabled:
            with self._ctx._resource_lock_guard:
                lock = self._ctx._resource_locks.setdefault(
                    self._resource_id,
                    threading.Lock(),
                )
            lock.acquire()
            self._lock = lock
        self._ctx._active.resource_id = self._resource_id
        self._started_ns = self._ctx._clock_ns()
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        elapsed_us = max(1, (self._ctx._clock_ns() - self._started_ns + 999) // 1000)
        entry = getattr(self._ctx._active, "entry", None)
        if entry is not None:
            if entry.resource_access_us is None:
                entry.resource_access_us = {}
            previous = entry.resource_access_us.get(self._resource_id, 0)
            if elapsed_us > previous:
                entry.resource_access_us[self._resource_id] = elapsed_us
        self._ctx._active.resource_id = None
        if self._lock is not None:
            self._lock.release()


class Runtime:
    """Reactive synchronous runtime with per-node scheduling metadata.

    Parameters
    ----------
    inputs:
        Optional shared inputs object.  Written by producers between ticks;
        snapshotted into ``ctx.latched_inputs`` at the start of each tick.
    executor:
        Optional ``concurrent.futures.Executor`` (e.g.
        ``ThreadPoolExecutor``).  When provided, each tick phase runs in
        parallel across all active nodes, with an implicit barrier between
        phases (``executor.map`` blocks until all tasks complete).
        When ``None`` (default), phases run sequentially.
    worker_pool:
        Optional ``WorkerPool`` for async deferred actions.  When provided,
        deferred actions are posted to the pool and the tick continues
        immediately — actions may run after dump, and their results arrive
        in ``ctx.inputs`` at the next latch.  When ``None`` (default),
        deferred actions run synchronously before dump.

    ``Runtime`` validates per-node periods/deadlines and computes the base
    period.  It does not own an activation table; multi-rate executors build
    the scheduling structure they need.

    Thread safety
    -------------
    ``tick()`` is **not** thread-safe.  Only one thread may call it at a
    time for a given Runtime.
    """

    __slots__ = (
        "context",
        "_entries",
        "_executor",
        "_worker_pool",
        "_built",
        "_period_us",
        "_nslots",
        "_current_slot",
        "_slots",
    )

    def __init__(
        self,
        inputs: Any = None,
        executor: Executor | None = None,
        worker_pool: WorkerPool | None = None,
    ) -> None:
        self.context      = Context(inputs)
        self._entries:    list[_NodeEntry] = []
        self._executor    = executor
        self._worker_pool = worker_pool
        self._built       = False
        self._period_us   = 0
        self._nslots      = 0
        self._current_slot = 0
        self._slots:      list[list[Any]] = []  # deprecated compatibility only

    # ------------------------------------------------------------------ #
    # Public API                                                           #
    # ------------------------------------------------------------------ #

    @property
    def nodes(self) -> list[Any]:
        """All registered nodes (in registration order)."""
        return [e.node for e in self._entries]

    def add_node(self, node: Any, period_us: int = 0, deadline_us: int = 0) -> None:
        """Register *node* with an optional period.

        Parameters
        ----------
        period_us:
            Period in microseconds.  ``0`` (default) = async.
        deadline_us:
            Relative deadline in microseconds. ``0`` means same as period.
        """
        self._entries.append(_NodeEntry(node=node, period_us=period_us, deadline_us=deadline_us))
        self._built = False

    def build(self) -> None:
        """Validate scheduling metadata and compute the runtime base period.

        Called automatically by the first :meth:`tick` if not called
        explicitly.  Must be called after the last :meth:`add_node`.

        Sets ``period_us`` to the GCD of all periodic node periods, or ``0``
        if all nodes are async.  No hyperperiod activation table is built.
        """
        entries = self._entries
        if not entries:
            self._period_us    = 0
            self._nslots       = 0
            self._current_slot = 0
            self._slots        = []
            self._built        = True
            return

        periods = [e.period_us for e in entries if e.period_us > 0]
        for e in entries:
            if e.period_us < 0:
                raise ValueError("period_us must be >= 0")
            if e.deadline_us < 0:
                raise ValueError("deadline_us must be >= 0")
            if e.period_us > 0 and e.deadline_us > e.period_us:
                raise ValueError("deadline_us must be <= period_us")

        if not periods:
            self._period_us    = 0
            self._nslots       = 0
            self._current_slot = 0
            self._slots        = []
            self._built        = True
            return

        base  = math.gcd(*periods)

        self._period_us    = base
        self._nslots       = 0
        self._current_slot = 0
        self._slots        = []
        self._built        = True

    @property
    def period_us(self) -> int:
        """Base period in microseconds (GCD of all periodic nodes).

        ``0`` if no periodic nodes have been registered (all async).
        Computed by :meth:`build`.
        """
        if not self._built:
            self.build()
        return self._period_us

    @property
    def nslots(self) -> int:
        """Deprecated runtime-owned slot count.  Always ``0`` after build."""
        if not self._built:
            self.build()
        return self._nslots

    def tick(self) -> None:
        """Execute one manual tick for all registered nodes.

        **Not thread-safe.** Only one thread may call ``tick()`` at a time.
        """
        if not self._built:
            self.build()

        self.tick_nodes_at(range(len(self._entries)), activation_us=0)

    def tick_nodes(self, node_indices: list[int] | range | tuple[int, ...]) -> None:
        self.tick_nodes_at(node_indices, activation_us=0)

    def tick_nodes_at(
        self,
        node_indices: list[int] | range | tuple[int, ...],
        activation_us: int,
    ) -> None:
        if not self._built:
            self.build()

        entries = [self._entries[i] for i in node_indices]
        self.context.activation_us = activation_us
        nodes = [e.node for e in entries]
        if self._executor is not None:
            elapsed = self._parallel_tick(entries)
        else:
            elapsed = self._sequential_tick(entries)
        for entry, elapsed_us in zip(entries, elapsed):
            entry.wcet_us = max(entry.wcet_us, elapsed_us)

    # ------------------------------------------------------------------ #
    # Internal                                                             #
    # ------------------------------------------------------------------ #

    def _sequential_tick(self, entries: list[_NodeEntry]) -> list[int]:
        ctx = self.context
        started: dict[int, int] = {}
        nodes = [e.node for e in entries]

        ctx.set_critical_section_locking(False)
        ctx.latch_inputs()
        for entry, node in zip(entries, nodes):
            ctx.set_active_entry(entry)
            started[id(node)] = time.perf_counter_ns()
            node.latch_inputs(ctx)

        for entry, node in zip(entries, nodes):
            ctx.set_active_entry(entry)
            node.evaluate(ctx)

        for entry, node in zip(entries, nodes):
            ctx.set_active_entry(entry)
            node.commit(ctx)
        ctx.set_active_entry(None)

        ctx.dispatch_deferred(self._worker_pool)

        elapsed: list[int] = []
        for node in nodes:
            node.dump_outputs(ctx)
            elapsed_us = max(1, (time.perf_counter_ns() - started[id(node)] + 999) // 1000)
            elapsed.append(elapsed_us)
        return elapsed

    def _parallel_tick(self, entries: list[_NodeEntry]) -> list[int]:
        """Parallel tick with an implicit barrier between each phase.

        executor.map() submits all tasks to the thread pool and blocks
        until every task in the phase completes — that blocking IS the
        barrier between phases.
        """
        ex  = self._executor
        ctx = self.context
        nodes = [e.node for e in entries]
        started = {id(n): time.perf_counter_ns() for n in nodes}
        entries_by_id = {id(e.node): e for e in entries}

        def run_phase(node: Any, phase: str) -> None:
            ctx.set_active_entry(entries_by_id[id(node)])
            getattr(node, phase)(ctx)
            ctx.set_active_entry(None)

        ctx.set_critical_section_locking(True)
        ctx.latch_inputs()
        list(ex.map(lambda n: run_phase(n, "latch_inputs"), nodes))  # type: ignore[union-attr]
        list(ex.map(lambda n: run_phase(n, "evaluate"), nodes))      # type: ignore[union-attr]
        list(ex.map(lambda n: run_phase(n, "commit"), nodes))        # type: ignore[union-attr]

        ctx.dispatch_deferred(self._worker_pool)

        list(ex.map(lambda n: run_phase(n, "dump_outputs"), nodes))  # type: ignore[union-attr]
        finished = time.perf_counter_ns()
        return [max(1, (finished - started[id(n)] + 999) // 1000) for n in nodes]

    def node_entries(self) -> list[_NodeEntry]:
        return self._entries
