# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""Tests for rxnet.worker_pool — WorkerPool and Priority."""
from __future__ import annotations

import time
import threading

import pytest

from rxnet.worker_pool import Priority, WorkerPool


class TestPriority:
    def test_critical_greater_than_high(self) -> None:
        assert Priority.CRITICAL > Priority.HIGH

    def test_high_greater_than_normal(self) -> None:
        assert Priority.HIGH > Priority.NORMAL

    def test_normal_greater_than_low(self) -> None:
        assert Priority.NORMAL > Priority.LOW


class TestWorkerPool:
    def test_submit_runs_action(self) -> None:
        done = threading.Event()
        with WorkerPool(num_workers=1) as pool:
            pool.submit(lambda ctx, user: done.set(), None, None)
            assert done.wait(timeout=2.0), "action did not run"

    def test_submit_passes_user_data(self) -> None:
        received: list[object] = []
        sentinel = object()
        done = threading.Event()

        def action(ctx: object, user: object) -> None:
            received.append(user)
            done.set()

        with WorkerPool(num_workers=1) as pool:
            pool.submit(action, None, sentinel)
            assert done.wait(timeout=2.0)

        assert received[0] is sentinel

    def test_high_priority_runs_before_low(self) -> None:
        """A HIGH action submitted after a LOW action must run first."""
        # Use a gate to hold the worker until both tasks are queued.
        gate  = threading.Event()
        order: list[str] = []
        done  = threading.Event()

        def low_action(ctx: object, user: object) -> None:
            gate.wait()        # block until both tasks are enqueued
            order.append("low")

        def high_action(ctx: object, user: object) -> None:
            gate.wait()
            order.append("high")
            done.set()

        with WorkerPool(num_workers=1) as pool:
            # Enqueue low first, then high — high should still run first.
            pool.submit(low_action,  None, None, Priority.LOW)
            pool.submit(high_action, None, None, Priority.HIGH)
            gate.set()          # release both; worker picks highest priority
            assert done.wait(timeout=2.0), "high action did not run"

        assert order == ["high", "low"]

    def test_fifo_within_same_priority(self) -> None:
        """Actions at the same priority run in submission order."""
        gate  = threading.Event()
        order: list[int] = []
        done  = threading.Event()

        def make_action(n: int) -> object:
            def action(ctx: object, user: object) -> None:
                gate.wait()
                order.append(n)
                if n == 3:
                    done.set()
            return action

        with WorkerPool(num_workers=1) as pool:
            for i in range(1, 4):
                pool.submit(make_action(i), None, None, Priority.NORMAL)
            gate.set()
            assert done.wait(timeout=2.0)

        assert order == [1, 2, 3]

    def test_context_manager_shuts_down(self) -> None:
        pool = WorkerPool(num_workers=2)
        with pool:
            pass
        # All worker threads should have exited.
        for w in pool._workers:
            assert not w.is_alive()

    def test_shutdown_wait_drains_queue(self) -> None:
        results: list[int] = []

        def action(ctx: object, user: object) -> None:
            time.sleep(0.01)
            results.append(user)  # type: ignore[arg-type]

        pool = WorkerPool(num_workers=2)
        for i in range(4):
            pool.submit(action, None, i)
        pool.shutdown(wait=True)

        assert sorted(results) == [0, 1, 2, 3]

    def test_multiple_workers_run_concurrently(self) -> None:
        """Four actions that each sleep 50 ms should finish in ~50 ms with 4 workers."""
        start = time.monotonic()
        done  = threading.Barrier(4 + 1)  # 4 workers + main thread

        def action(ctx: object, user: object) -> None:
            time.sleep(0.05)
            done.wait(timeout=2.0)

        with WorkerPool(num_workers=4) as pool:
            for _ in range(4):
                pool.submit(action, None, None)
            done.wait(timeout=2.0)

        elapsed = time.monotonic() - start
        # Sequential would take ~200 ms; parallel should finish in ~50–100 ms.
        assert elapsed < 0.15, f"workers did not run concurrently ({elapsed:.3f}s)"
