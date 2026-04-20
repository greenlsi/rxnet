# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

from __future__ import annotations

import copy
import math
import threading
from concurrent.futures import Executor
from dataclasses import dataclass
from typing import Any, Protocol

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

    __slots__ = ("inputs", "latched_inputs", "_deferred_actions", "_deferred_lock")

    def __init__(self, inputs: Any = None) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)
        self._deferred_actions: list[DeferredAction] = []
        self._deferred_lock = threading.Lock()

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


class Runtime:
    """Reactive synchronous runtime with per-node multi-rate scheduling.

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

    Multi-rate scheduling
    ---------------------
    Each node is registered with an optional ``period_us`` (microseconds).
    Call :meth:`build` (or let the first :meth:`tick` call it automatically)
    to compute the hyperperiod dispatch table::

        base_us  = GCD of all periodic node periods  → ``period_us``
        hyper_us = LCM of all periodic node periods
        nslots   = hyper_us / base_us

    ``period_us == 0`` marks a node as *async*: it runs on every base tick
    regardless of slot.

    :meth:`tick` advances the internal slot counter automatically.  The
    executors (``CyclicExecutive``, ``CoopExecutive``, ``ThreadExecutive``)
    call ``tick()`` at the right intervals based on ``runtime.period_us``.

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
        self._nslots      = 1
        self._current_slot = 0
        self._slots:      list[list[Any]] = []  # slots[s] = list of Node

    # ------------------------------------------------------------------ #
    # Public API                                                           #
    # ------------------------------------------------------------------ #

    @property
    def nodes(self) -> list[Any]:
        """All registered nodes (in registration order)."""
        return [e.node for e in self._entries]

    def add_node(self, node: Any, period_us: int = 0) -> None:
        """Register *node* with an optional period.

        Parameters
        ----------
        period_us:
            Period in microseconds.  ``0`` (default) = async: the node
            runs on every base tick regardless of slot.  A positive value
            makes the node periodic; the hyperperiod table is updated on
            the next :meth:`build` call.
        """
        self._entries.append(_NodeEntry(node=node, period_us=period_us))
        self._built = False  # invalidate slot table

    def build(self) -> None:
        """Compute the hyperperiod dispatch table.

        Called automatically by the first :meth:`tick` if not called
        explicitly.  Must be called after the last :meth:`add_node`.

        Sets:
        * ``period_us`` — GCD of all periodic node periods (0 if all async).
        * ``nslots``    — number of hyperperiod slots (1 if all async).
        """
        entries = self._entries
        if not entries:
            self._period_us    = 0
            self._nslots       = 1
            self._current_slot = 0
            self._slots        = [[]]
            self._built        = True
            return

        periods = [e.period_us for e in entries if e.period_us > 0]

        if not periods:
            # All async: one slot, base period undefined.
            self._period_us    = 0
            self._nslots       = 1
            self._current_slot = 0
            self._slots        = [[e.node for e in entries]]
            self._built        = True
            return

        base  = math.gcd(*periods)
        hyper = math.lcm(*periods)
        nslots = hyper // base

        slots: list[list[Any]] = []
        for s in range(nslots):
            active: list[Any] = []
            for e in entries:
                if e.period_us == 0:
                    active.append(e.node)           # async: every slot
                elif (s * base) % e.period_us == 0:
                    active.append(e.node)           # periodic: scheduled slot
            slots.append(active)

        self._period_us    = base
        self._nslots       = nslots
        self._current_slot = 0
        self._slots        = slots
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
        """Number of hyperperiod slots.  ``1`` if all nodes are async."""
        if not self._built:
            self.build()
        return self._nslots

    def tick(self) -> None:
        """Execute one tick for the current hyperperiod slot.

        Runs all nodes active in ``current_slot``, then advances the slot
        counter.  The first call triggers :meth:`build` if not yet built.

        **Not thread-safe.** Only one thread may call ``tick()`` at a time.
        """
        if not self._built:
            self.build()

        nodes = self._slots[self._current_slot]
        if self._executor is not None:
            self._parallel_tick(nodes)
        else:
            self._sequential_tick(nodes)

        self._current_slot = (self._current_slot + 1) % self._nslots

    # ------------------------------------------------------------------ #
    # Internal                                                             #
    # ------------------------------------------------------------------ #

    def _sequential_tick(self, nodes: list[Any]) -> None:
        ctx = self.context

        ctx.latch_inputs()
        for node in nodes:
            node.latch_inputs(ctx)

        for node in nodes:
            node.evaluate(ctx)

        for node in nodes:
            node.commit(ctx)

        ctx.dispatch_deferred(self._worker_pool)

        for node in nodes:
            node.dump_outputs(ctx)

    def _parallel_tick(self, nodes: list[Any]) -> None:
        """Parallel tick with an implicit barrier between each phase.

        executor.map() submits all tasks to the thread pool and blocks
        until every task in the phase completes — that blocking IS the
        barrier between phases.
        """
        ex  = self._executor
        ctx = self.context

        ctx.latch_inputs()
        list(ex.map(lambda n: n.latch_inputs(ctx), nodes))  # type: ignore[union-attr]
        list(ex.map(lambda n: n.evaluate(ctx), nodes))       # type: ignore[union-attr]
        list(ex.map(lambda n: n.commit(ctx), nodes))         # type: ignore[union-attr]

        ctx.dispatch_deferred(self._worker_pool)

        list(ex.map(lambda n: n.dump_outputs(ctx), nodes))  # type: ignore[union-attr]
