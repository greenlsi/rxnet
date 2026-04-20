# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

from __future__ import annotations

import heapq
import threading
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any, Callable


class Priority(IntEnum):
    """Action priority for the worker pool.

    Higher value = runs first. Actions with the same priority run in FIFO
    order (submission order is preserved).
    """

    LOW      = 0
    NORMAL   = 1
    HIGH     = 2
    CRITICAL = 3


Action = Callable[["Context", Any], None]  # type: ignore[type-arg]  # forward ref


@dataclass(order=True)
class _Task:
    # Min-heap: store negated priority so CRITICAL (-3) sorts before LOW (0).
    _neg_priority: int
    _sequence: int          # FIFO tiebreaker within the same priority level
    fn:   Action = field(compare=False)
    ctx:  Any    = field(compare=False)
    user: Any    = field(compare=False)


class WorkerPool:
    """Priority thread pool for async deferred actions.

    Actions submitted with a higher ``Priority`` run before lower-priority
    ones. Within the same priority, FIFO order (submission order) is
    preserved.

    Thread safety
    -------------
    ``submit()`` is thread-safe and may be called from any thread, including
    from inside a deferred action running in the pool itself.

    Usage::

        pool = WorkerPool(num_workers=4)
        rt = Runtime(worker_pool=pool)
        ...
        pool.shutdown()

    Or as a context manager::

        with WorkerPool(num_workers=4) as pool:
            rt = Runtime(worker_pool=pool)
            ...

    Async actions and context access
    ---------------------------------
    Async actions receive ``(ctx, user)`` matching the standard callback
    signature. Because async actions may run *after* the next tick has
    started, they must treat ``ctx`` as potentially concurrent:

    * **Safe**: reading ``ctx.latched_inputs`` (captured at latch time).
    * **Safe**: writing to ``user`` fields not shared with other nodes.
    * **Unsafe**: writing to ``ctx.inputs`` without an external lock
      (use atomic replacement: ``ctx.inputs["key"] = new_value``).
    """

    def __init__(self, num_workers: int = 4) -> None:
        self._heap:     list[_Task] = []
        self._lock      = threading.Lock()
        self._not_empty = threading.Condition(self._lock)
        self._sequence  = 0
        self._shutdown  = False
        self._workers   = [
            threading.Thread(
                target=self._loop,
                daemon=True,
                name=f"rxnet-worker-{i}",
            )
            for i in range(num_workers)
        ]
        for w in self._workers:
            w.start()

    def submit(
        self,
        fn: Action,
        ctx: Any,
        user: Any,
        priority: Priority = Priority.NORMAL,
    ) -> None:
        """Enqueue an action. Thread-safe. Returns immediately."""
        with self._not_empty:
            heapq.heappush(
                self._heap,
                _Task(
                    _neg_priority=-int(priority),
                    _sequence=self._sequence,
                    fn=fn,
                    ctx=ctx,
                    user=user,
                ),
            )
            self._sequence += 1
            self._not_empty.notify()

    def shutdown(self, wait: bool = True) -> None:
        """Stop the pool. If *wait* is True, block until all queued actions finish."""
        with self._not_empty:
            self._shutdown = True
            self._not_empty.notify_all()
        if wait:
            for w in self._workers:
                w.join()

    def __enter__(self) -> WorkerPool:
        return self

    def __exit__(self, *_: object) -> None:
        self.shutdown()

    # ------------------------------------------------------------------ #
    # Internal                                                             #
    # ------------------------------------------------------------------ #

    def _loop(self) -> None:
        while True:
            with self._not_empty:
                while not self._heap and not self._shutdown:
                    self._not_empty.wait()
                if self._shutdown and not self._heap:
                    return
                task = heapq.heappop(self._heap)
            task.fn(task.ctx, task.user)
