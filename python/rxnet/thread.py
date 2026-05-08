# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""rxnet.thread — BSP thread-per-node executor with phase barriers.

One thread per node.  Three ``threading.Barrier`` objects per hyperperiod
slot enforce the reactive-synchronous guarantee:

- **latch_b[s]**: all active nodes arrive → action: global latch
  (``ctx.latch_inputs()``) → all released → each node runs
  ``latch_inputs`` + ``evaluate`` in parallel.
- **commit_b[s]**: all active nodes arrive → no action → all released →
  each node runs ``commit`` in parallel.
- **dump_b[s]**: all active nodes arrive → action: dispatch deferred
  actions → all released → each node runs ``dump_outputs`` in parallel.

The three barriers map cleanly to the five reactive phases:

  latch_b  →  latch_inputs (global + per-node) + evaluate
  commit_b  →  commit
  dump_b   →  (deferred dispatch) + dump_outputs

Multiple runtimes can be registered; each forms an independent barrier
group.  Nodes in different runtimes run fully independently (no shared
barriers or contexts across runtime groups).

The last node of the last runtime runs in the calling (main) thread,
preserving stdin/CLI access.

Usage::

    from rxnet.thread import ThreadExecutive

    te = ThreadExecutive()
    te.add(rt)          # one runtime: all nodes get threads except the last
    te.run()            # returns when te.stop() is requested

Multiple runtimes::

    te.add(pn_rt)       # PN nodes → background threads
    te.add(cli_rt)      # cli → main thread (last runtime, last node)
    te.run()
"""
from __future__ import annotations

import threading
import time
from collections.abc import Callable
from typing import Any

from .cyclic import sleep_until


class ThreadExecutive:
    """BSP thread-per-node executor.

    Each node in each registered runtime runs in its own ``threading.Thread``
    (except the very last node of the very last runtime, which runs in the
    calling thread).  Phase barriers ensure the reactive-synchronous
    invariant: all latches and evaluations complete before any commit, and
    all commits complete before deferred dispatch and dump.

    .. note::
        Python's GIL means threads cannot run Python bytecode truly in
        parallel.  For I/O-bound latch/dump phases (reading GPIO, writing
        hardware) the GIL is released and true parallelism is achieved.
        For purely compute-bound node logic, the sequential or parallel
        (``executor``) tick variants of :class:`~rxnet.runtime.Runtime`
        may be more suitable.
    """

    def __init__(self, on_stop: Callable[[], None] | None = None) -> None:
        self._runtimes: list[Any] = []
        self._stop_requested = threading.Event()
        self._on_stop = on_stop

    def stop(self) -> None:
        """Request all node loops to stop at a hyperperiod boundary."""
        self._stop_requested.set()

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
        """Spawn node threads and enter the main-thread node loop.

        The last node of the last registered runtime runs in the calling
        thread.  Returns when :meth:`stop` is requested.
        """
        if not self._runtimes:
            raise RuntimeError("no runtimes registered")

        t0 = time.monotonic()
        threads: list[threading.Thread] = []
        main_loop_fn: Any = None

        total_groups = len(self._runtimes)
        all_barriers: list[threading.Barrier] = []

        self._stop_requested.clear()

        for group_idx, rt in enumerate(self._runtimes):
            ctx     = rt.context
            entries = rt._entries
            nslots  = rt.nslots
            base_us = rt.period_us

            # Build per-slot barriers.
            # latch_b[s]: action = global latch (ctx.latch_inputs)
            # commit_b[s]: no action (just synchronises commit phase)
            # dump_b[s]: action = dispatch deferred actions
            latch_bs: list[threading.Barrier | None]  = []
            commit_bs: list[threading.Barrier | None] = []
            dump_bs: list[threading.Barrier | None]   = []

            for s in range(nslots):
                n = len(rt._slots[s])
                if n == 0:
                    latch_bs.append(None)
                    commit_bs.append(None)
                    dump_bs.append(None)
                else:
                    # Capture ctx in closure via default arg.
                    lb = threading.Barrier(n, action=lambda c=ctx: c.latch_inputs())  # type: ignore[misc]
                    cb = threading.Barrier(n)
                    db = threading.Barrier(
                        n,
                        action=lambda c=ctx, wp=rt._worker_pool: c.dispatch_deferred(wp),  # type: ignore[misc]
                    )
                    latch_bs.append(lb)
                    commit_bs.append(cb)
                    dump_bs.append(db)
                    all_barriers.extend([lb, cb, db])

            total_nodes = len(entries)

            for node_idx, entry in enumerate(entries):
                node     = entry.node
                period_s = (entry.period_us if entry.period_us > 0 else base_us) * 1e-6
                step     = (entry.period_us // base_us) if entry.period_us > 0 else 1

                is_last = (
                    group_idx == total_groups - 1
                    and node_idx == total_nodes - 1
                )

                def make_loop(
                    node=node,
                    ctx=ctx,
                    period_s=period_s,
                    step=step,
                    latch_bs=latch_bs,
                    commit_bs=commit_bs,
                    dump_bs=dump_bs,
                    nslots=nslots,
                    stop_requested=self._stop_requested,
                ) -> None:
                    next_tick = t0
                    base_tick = 0
                    while True:
                        sleep_until(next_tick)
                        slot = base_tick % nslots

                        lb = latch_bs[slot]
                        cb = commit_bs[slot]
                        db = dump_bs[slot]

                        if lb is not None:
                            try:
                                lb.wait()
                            except threading.BrokenBarrierError:
                                return
                        node.latch_inputs(ctx)
                        node.evaluate(ctx)

                        if cb is not None:
                            try:
                                cb.wait()
                            except threading.BrokenBarrierError:
                                return
                        node.commit(ctx)

                        if db is not None:
                            try:
                                db.wait()
                            except threading.BrokenBarrierError:
                                return
                        node.dump_outputs(ctx)

                        next_tick += period_s
                        base_tick += step
                        if stop_requested.is_set() and base_tick % nslots == 0:
                            return

                if is_last:
                    main_loop_fn = make_loop
                else:
                    t = threading.Thread(target=make_loop, daemon=True)
                    threads.append(t)
                    t.start()

        # The last node runs in the calling (main) thread.
        assert main_loop_fn is not None
        try:
            main_loop_fn()
        finally:
            self._stop_requested.set()
            for b in all_barriers:
                b.abort()
            for t in threads:
                t.join(timeout=2.0)
            if self._on_stop is not None:
                self._on_stop()
