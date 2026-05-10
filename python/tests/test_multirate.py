# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""Tests for multi-rate scheduling: Runtime.build(), per-node period_us,
CyclicExecutive, CoopExecutive, and ThreadExecutive.

These tests mirror the C-side multi-rate tests and verify:
  - Runtime metadata without materialized activation table
  - Executor-owned activation groups
  - Executors drive runtimes at correct rates
"""
from __future__ import annotations

import sys
import io
import threading
import time
from contextlib import contextmanager
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import pytest

from rxnet.runtime import Context, Runtime, SchedReport, print_sched_report

# ------------------------------------------------------------------ #
# Helpers                                                              #
# ------------------------------------------------------------------ #


class CountingNode:
    """Records tick counts and slot participation."""

    def __init__(self, name: str) -> None:
        self.name   = name
        self.ticks  = 0
        self.order: list[str] = []
        self.activation_us: list[int] = []

    def latch_inputs(self, ctx: Context) -> None:
        self.activation_us.append(ctx.activation_us)

    def evaluate(self, ctx: Context) -> None:
        pass

    def commit(self, ctx: Context) -> None:
        pass

    def dump_outputs(self, ctx: Context) -> None:
        self.ticks += 1
        self.order.append(self.name)


class CriticalNode(CountingNode):
    def __init__(self, name: str, resources: dict[int, int] | None = None) -> None:
        super().__init__(name)
        self.resources = resources or {}

    def evaluate(self, ctx: Context) -> None:
        for resource_id, elapsed_us in self.resources.items():
            with fake_critical_time(ctx, elapsed_us):
                with ctx.critical_section(resource_id):
                    pass


@contextmanager
def fake_critical_time(ctx: Context, elapsed_us: int):
    calls = iter([1_000_000_000, 1_000_000_000 + elapsed_us * 1000])
    original = ctx._clock_ns
    ctx._clock_ns = lambda: next(calls)
    try:
        yield
    finally:
        ctx._clock_ns = original


# ------------------------------------------------------------------ #
# Runtime.build() — hyperperiod table                                  #
# ------------------------------------------------------------------ #


class TestRuntimeBuild:
    def test_all_async_nodes_have_no_base_period(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"))  # period_us = 0 → async
        rt.build()
        assert rt.nslots == 0
        assert rt.period_us == 0

    def test_single_period_sets_base_without_slots(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        rt.build()
        assert rt.nslots == 0
        assert rt.period_us == 10_000

    def test_two_equal_periods_set_base_without_slots(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        rt.add_node(CountingNode("b"), period_us=10_000)
        rt.build()
        assert rt.nslots == 0
        assert rt.period_us == 10_000

    def test_two_periods_ratio_2_gives_gcd_base_only(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("fast"), period_us=10_000)
        rt.add_node(CountingNode("slow"), period_us=20_000)
        rt.build()
        assert rt.period_us == 10_000
        assert rt.nslots == 0

    def test_three_periods_gcd_lcm(self) -> None:
        """GCD(10,20,20)=10."""
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        rt.add_node(CountingNode("b"), period_us=20_000)
        rt.add_node(CountingNode("c"), period_us=20_000)
        rt.build()
        assert rt.period_us == 10_000
        assert rt.nslots == 0

    def test_build_called_automatically_on_first_tick(self) -> None:
        fast = CountingNode("fast")
        slow = CountingNode("slow")
        rt = Runtime()
        rt.add_node(fast, period_us=10_000)
        rt.add_node(slow, period_us=20_000)
        # No explicit build() call
        rt.tick()
        assert rt._built

    def test_add_node_invalidates_build(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        rt.build()
        assert rt._built
        rt.add_node(CountingNode("b"), period_us=10_000)
        assert not rt._built  # adding a node invalidates the table

    def test_period_us_triggers_build(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        assert not rt._built
        _ = rt.period_us  # accessing property triggers build
        assert rt._built

    def test_nslots_triggers_build(self) -> None:
        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        assert not rt._built
        _ = rt.nslots
        assert rt._built


# ------------------------------------------------------------------ #
# Runtime.tick() — manual all-node execution                            #
# ------------------------------------------------------------------ #


class TestManualTick:
    def test_tick_runs_all_nodes(self) -> None:
        fast = CountingNode("fast")
        slow = CountingNode("slow")
        rt = Runtime()
        rt.add_node(fast, period_us=10_000)
        rt.add_node(slow, period_us=20_000)

        for _ in range(3):
            rt.tick()

        assert fast.ticks == 3
        assert slow.ticks == 3

    def test_async_node_runs_every_tick(self) -> None:
        always = CountingNode("always")
        slow   = CountingNode("slow")
        rt = Runtime()
        rt.add_node(always, period_us=0)
        rt.add_node(slow,   period_us=20_000)

        for _ in range(4):
            rt.tick()

        assert always.ticks == 4   # async: every tick
        assert slow.ticks   == 4

    def test_async_node_runs_every_tick_with_multirate(self) -> None:
        always = CountingNode("always")
        fast   = CountingNode("fast")
        slow   = CountingNode("slow")
        rt = Runtime()
        rt.add_node(always, period_us=0)       # async
        rt.add_node(fast,   period_us=10_000)
        rt.add_node(slow,   period_us=20_000)

        for _ in range(4):
            rt.tick()

        assert always.ticks == 4  # async: every slot
        assert fast.ticks   == 4  # 10 ms: every slot
        assert slow.ticks   == 4

    def test_backward_compat_no_period(self) -> None:
        """Nodes without period_us (default 0 = async) tick every call."""
        a = CountingNode("a")
        b = CountingNode("b")
        rt = Runtime()
        rt.add_node(a)  # no period_us — backward compatible
        rt.add_node(b)

        for _ in range(5):
            rt.tick()

        assert a.ticks == 5
        assert b.ticks == 5


# ------------------------------------------------------------------ #
# CyclicExecutive                                                      #
# ------------------------------------------------------------------ #


class TestCyclicExecutive:
    def test_rejects_runtime_with_no_periodic_nodes(self) -> None:
        from rxnet.cyclic import CyclicExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"))  # async only
        rt.build()

        ce = CyclicExecutive()
        with pytest.raises(ValueError, match="no periodic nodes"):
            ce.add(rt)

    def test_rejects_empty_executive(self) -> None:
        from rxnet.cyclic import CyclicExecutive

        ce = CyclicExecutive()
        with pytest.raises(RuntimeError, match="no runtimes"):
            ce.run()

    def test_add_builds_runtime_if_needed(self) -> None:
        from rxnet.cyclic import CyclicExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        assert not rt._built

        ce = CyclicExecutive()
        ce.add(rt)
        assert rt._built

    def test_outer_slot_table_two_runtimes_different_periods(self) -> None:
        """Two runtimes with periods 10 ms and 20 ms → 2 outer slots.
        Slot 0: both. Slot 1: only fast."""
        import math

        from rxnet.cyclic import CyclicExecutive

        fast_rt = Runtime()
        fast_rt.add_node(CountingNode("f"), period_us=10_000)

        slow_rt = Runtime()
        slow_rt.add_node(CountingNode("s"), period_us=20_000)

        ce = CyclicExecutive()
        ce.add(fast_rt)
        ce.add(slow_rt)

        periods  = [fast_rt.period_us, slow_rt.period_us]
        base_us  = math.gcd(*periods)
        hyper_us = math.lcm(*periods)
        n_slots  = hyper_us // base_us

        assert base_us  == 10_000
        assert hyper_us == 20_000
        assert n_slots  == 2

    def test_stop_returns_and_runs_callback(self) -> None:
        from rxnet.cyclic import CyclicExecutive

        stopped = threading.Event()
        ce = CyclicExecutive(on_stop=stopped.set)

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                ce.stop()

        rt = Runtime()
        node = StopNode("a")
        rt.add_node(node, period_us=10_000)

        ce.add(rt)
        ce.run()

        assert node.ticks == 1
        assert stopped.is_set()

    def test_single_runtime_multirate_activations(self) -> None:
        from rxnet.cyclic import CyclicExecutive

        stopped = threading.Event()
        ce = CyclicExecutive(on_stop=stopped.set)
        fast = CountingNode("fast")
        slow = CountingNode("slow")

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                if self.ticks >= 3:
                    ce.stop()

        cli = StopNode("cli")
        rt = Runtime()
        rt.add_node(fast, period_us=10_000)
        rt.add_node(slow, period_us=20_000)
        rt.add_node(cli, period_us=10_000)
        ce.add(rt)
        ce.run()

        assert fast.ticks == 3
        assert slow.ticks == 2
        assert cli.ticks == 3
        assert slow.activation_us[:2] == [0, 20_000]


# ------------------------------------------------------------------ #
# CoopExecutive                                                        #
# ------------------------------------------------------------------ #


class TestCoopExecutive:
    def test_rejects_runtime_with_no_periodic_nodes(self) -> None:
        from rxnet.coop import CoopExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"))
        rt.build()

        ce = CoopExecutive()
        with pytest.raises(ValueError, match="no periodic nodes"):
            ce.add(rt)

    def test_rejects_empty_executive(self) -> None:
        from rxnet.coop import CoopExecutive

        ce = CoopExecutive()
        with pytest.raises(RuntimeError, match="no runtimes"):
            ce.run()

    def test_add_builds_runtime_if_needed(self) -> None:
        from rxnet.coop import CoopExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        assert not rt._built

        ce = CoopExecutive()
        ce.add(rt)
        assert rt._built

    def test_stop_returns_and_runs_callback(self) -> None:
        from rxnet.coop import CoopExecutive

        stopped = threading.Event()
        ce = CoopExecutive(on_stop=stopped.set)

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                ce.stop()

        rt = Runtime()
        node = StopNode("a")
        rt.add_node(node, period_us=10_000)

        ce.add(rt)
        ce.run()

        assert node.ticks == 1
        assert stopped.is_set()

    def test_single_runtime_multirate_activations(self) -> None:
        from rxnet.coop import CoopExecutive

        ce = CoopExecutive()
        fast = CountingNode("fast")
        slow = CountingNode("slow")

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                if self.ticks >= 3:
                    ce.stop()

        cli = StopNode("cli")
        rt = Runtime()
        rt.add_node(fast, period_us=10_000)
        rt.add_node(slow, period_us=20_000)
        rt.add_node(cli, period_us=10_000)
        ce.add(rt)
        ce.run()

        assert fast.ticks == 3
        assert slow.ticks == 2
        assert cli.ticks == 3
        assert slow.activation_us[:2] == [0, 20_000]


# ------------------------------------------------------------------ #
# ThreadExecutive                                                      #
# ------------------------------------------------------------------ #


class TestThreadExecutive:
    def test_rejects_runtime_with_no_periodic_nodes(self) -> None:
        from rxnet.thread import ThreadExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"))
        rt.build()

        te = ThreadExecutive()
        with pytest.raises(ValueError, match="no periodic nodes"):
            te.add(rt)

    def test_rejects_async_nodes(self) -> None:
        from rxnet.thread import ThreadExecutive

        rt = Runtime()
        rt.add_node(CountingNode("always"))
        rt.add_node(CountingNode("periodic"), period_us=10_000)

        te = ThreadExecutive()
        with pytest.raises(ValueError, match="async nodes"):
            te.add(rt)

    def test_rejects_empty_executive(self) -> None:
        from rxnet.thread import ThreadExecutive

        te = ThreadExecutive()
        with pytest.raises(RuntimeError, match="no runtimes"):
            te.run()

    def test_add_builds_runtime_if_needed(self) -> None:
        from rxnet.thread import ThreadExecutive

        rt = Runtime()
        rt.add_node(CountingNode("a"), period_us=10_000)
        assert not rt._built

        te = ThreadExecutive()
        te.add(rt)
        assert rt._built

    def test_stop_returns_and_runs_callback(self) -> None:
        from rxnet.thread import ThreadExecutive

        stopped = threading.Event()
        te = ThreadExecutive(on_stop=stopped.set)

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                te.stop()

        rt = Runtime()
        node = StopNode("a")
        rt.add_node(node, period_us=10_000)

        te.add(rt)
        te.run()

        assert node.ticks == 1
        assert stopped.is_set()

    def test_multirate_activation_group_shares_activation_time(self) -> None:
        from rxnet.thread import ThreadExecutive

        te = ThreadExecutive()
        fast = CountingNode("fast")
        slow = CountingNode("slow")

        class StopNode(CountingNode):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                if self.ticks >= 3:
                    te.stop()

        cli = StopNode("cli")
        rt = Runtime()
        rt.add_node(fast, period_us=10_000)
        rt.add_node(slow, period_us=20_000)
        rt.add_node(cli, period_us=10_000)
        te.add(rt)
        te.run()

        assert fast.activation_us[:3] == [0, 10_000, 20_000]
        assert slow.activation_us[:2] == [0, 20_000]
        assert cli.activation_us[:3] == [0, 10_000, 20_000]

    def test_bsp_global_latch_before_per_node_latch(self) -> None:
        """The global latch snapshot is captured before any per-node latch_inputs runs.

        All nodes must see the value that was in ctx.inputs at the moment the
        latch_b barrier fired — not a later value written between per-node latches.
        """
        from rxnet.thread import ThreadExecutive

        class SimpleInputs:
            def __init__(self) -> None:
                self.value = 42

        inputs = SimpleInputs()
        rt     = Runtime(inputs=inputs)

        PERIOD_US = 10_000
        n_nodes   = 3
        seen: list[int] = []
        lock = threading.Lock()
        done_event = threading.Event()
        tick_count = [0]

        class SnapshotNode:
            def latch_inputs(self, ctx: Context) -> None:
                # By the time per-node latch runs, latched_inputs is already
                # captured (by global latch in the barrier action).
                with lock:
                    seen.append(ctx.latched_inputs.value)

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                pass

            def dump_outputs(self, ctx: Context) -> None:
                with lock:
                    tick_count[0] += 1
                    if tick_count[0] >= n_nodes:
                        done_event.set()

        for _ in range(n_nodes):
            rt.add_node(SnapshotNode(), period_us=PERIOD_US)

        te = ThreadExecutive()
        te.add(rt)

        t = threading.Thread(target=te.run, daemon=True)
        t.start()

        assert done_event.wait(timeout=5.0), "ThreadExecutive did not complete in time"

        # All n_nodes in the first tick must see value=42.
        assert len(seen) >= n_nodes
        assert all(v == 42 for v in seen[:n_nodes]), f"Unexpected snapshots: {seen[:n_nodes]}"

    def test_bsp_commit_before_dump(self) -> None:
        """All commit phases complete before any dump_outputs begins."""
        from rxnet.thread import ThreadExecutive

        PERIOD_US = 10_000
        TICKS     = 3
        n_nodes   = 3

        commit_times: list[float] = []
        dump_times:   list[float] = []
        lock = threading.Lock()
        tick_count = [0]
        done_event = threading.Event()

        class TimedNode:
            def latch_inputs(self, ctx: Context) -> None:
                pass

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                time.sleep(0.001)
                with lock:
                    commit_times.append(time.monotonic())

            def dump_outputs(self, ctx: Context) -> None:
                with lock:
                    dump_times.append(time.monotonic())
                    tick_count[0] += 1
                    if tick_count[0] >= TICKS * n_nodes:
                        done_event.set()

        rt = Runtime()
        for _ in range(n_nodes):
            rt.add_node(TimedNode(), period_us=PERIOD_US)

        te = ThreadExecutive()
        te.add(rt)

        t = threading.Thread(target=te.run, daemon=True)
        t.start()

        assert done_event.wait(timeout=5.0)

        first_commits = sorted(commit_times)[:n_nodes]
        first_dumps   = sorted(dump_times)[:n_nodes]
        assert max(first_commits) < min(first_dumps), (
            f"Dump started before all commits finished: "
            f"last_commit={max(first_commits):.6f}, first_dump={min(first_dumps):.6f}"
        )

    def test_all_nodes_see_same_latched_snapshot(self) -> None:
        """All nodes in the same slot must see the same latched_inputs value."""
        from rxnet.thread import ThreadExecutive

        class SimpleInputs:
            def __init__(self) -> None:
                self.counter = 0

        inputs = SimpleInputs()
        rt     = Runtime(inputs=inputs)

        PERIOD_US = 10_000
        TICKS     = 5
        n_nodes   = 3

        seen: list[int] = []
        lock = threading.Lock()
        tick_count = [0]
        done_event = threading.Event()

        class SnapshotNode:
            def latch_inputs(self, ctx: Context) -> None:
                with lock:
                    seen.append(ctx.latched_inputs.counter)
                    tick_count[0] += 1
                    if tick_count[0] >= TICKS * n_nodes:
                        done_event.set()

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                pass

            def dump_outputs(self, ctx: Context) -> None:
                # Advance counter between ticks so each tick's snapshot is distinct.
                ctx.inputs.counter += 1

        for _ in range(n_nodes):
            rt.add_node(SnapshotNode(), period_us=PERIOD_US)

        te = ThreadExecutive()
        te.add(rt)

        t = threading.Thread(target=te.run, daemon=True)
        t.start()

        assert done_event.wait(timeout=5.0)

        # All n_nodes nodes in each tick must have seen the same counter value.
        # Group by slot: seen[0..n-1] are slot 0, seen[n..2n-1] are slot 1, etc.
        for slot_start in range(0, TICKS * n_nodes, n_nodes):
            group = seen[slot_start:slot_start + n_nodes]
            assert len(set(group)) == 1, (
                f"Nodes saw different snapshots in same slot: {group}"
            )

    def test_thread_sched_blocking_uses_measured_critical_sections(self) -> None:
        from rxnet.thread import ThreadExecutive

        high = CriticalNode("high")
        low_a = CriticalNode("low_a", {1: 300, 2: 700})
        low_b = CriticalNode("low_b", {1: 500})
        rt = Runtime()
        rt.add_node(high, period_us=10_000, deadline_us=10_000)
        rt.add_node(low_a, period_us=20_000, deadline_us=20_000)
        rt.add_node(low_b, period_us=30_000, deadline_us=30_000)
        rt.tick()
        rt._entries[0].wcet_us = 1000
        rt._entries[1].wcet_us = 1000
        rt._entries[2].wcet_us = 1000

        te = ThreadExecutive()
        te.add(rt)
        report = SchedReport()

        status = te.check_schedulability(report)

        assert status == 1
        assert report.tasks[0].blocking_us == 1200

    def test_sched_report_prints_resource_access_maxima(self) -> None:
        node = CriticalNode("node", {7: 250})
        rt = Runtime()
        rt.add_node(node, period_us=10_000)
        rt.tick()
        rt._entries[0].wcet_us = 1000

        from rxnet.coop import CoopExecutive

        ce = CoopExecutive()
        ce.add(rt)
        report = SchedReport()
        status = ce.check_schedulability(report)
        out = io.StringIO()

        print_sched_report("coop", status, report, out)

        text = out.getvalue()
        assert "resources:" in text
        assert "resource=7 max=250us" in text


# ------------------------------------------------------------------ #
# FSM Runtime with per-node periods                                    #
# ------------------------------------------------------------------ #


class TestFsmRuntimeMultiRate:
    def test_add_machine_with_period(self) -> None:
        from rxnet.fsm import Machine, Runtime

        rt = Runtime()
        m  = Machine("light", state=0, transitions=[])
        rt.add_machine(m, period_us=10_000)
        rt.build()

        assert rt.period_us == 10_000
        assert rt.nslots    == 0

    def test_two_machines_different_periods(self) -> None:
        from rxnet.fsm import Machine, Runtime

        rt   = Runtime()
        fast = Machine("fast", state=0, transitions=[])
        slow = Machine("slow", state=0, transitions=[])
        rt.add_machine(fast, period_us=10_000)
        rt.add_machine(slow, period_us=20_000)
        rt.build()

        assert rt.period_us == 10_000
        assert rt.nslots    == 0

    def test_slow_machine_ticks_half_as_often(self) -> None:
        """slow machine added at 20 ms should tick half as often as fast (10 ms)."""
        from rxnet.fsm import Machine, Runtime

        fast_ticks = [0]
        slow_ticks = [0]

        class FastMachine(Machine):
            def dump_outputs(self, ctx: Context) -> None:
                fast_ticks[0] += 1

        class SlowMachine(Machine):
            def dump_outputs(self, ctx: Context) -> None:
                slow_ticks[0] += 1

        fast = FastMachine("fast", state=0, transitions=[],
                           latch_inputs_cb=None, dump_outputs_cb=None)
        slow = SlowMachine("slow", state=0, transitions=[],
                           latch_inputs_cb=None, dump_outputs_cb=None)

        rt = Runtime()
        rt.add_machine(fast, period_us=10_000)
        rt.add_machine(slow, period_us=20_000)

        from rxnet.cyclic import CyclicExecutive

        ce = CyclicExecutive()

        class StopMachine(FastMachine):
            def dump_outputs(self, ctx: Context) -> None:
                super().dump_outputs(ctx)
                if fast_ticks[0] >= 6:
                    ce.stop()

        fast = StopMachine("fast", state=0, transitions=[],
                           latch_inputs_cb=None, dump_outputs_cb=None)
        rt = Runtime()
        rt.add_machine(fast, period_us=10_000)
        rt.add_machine(slow, period_us=20_000)
        ce.add(rt)
        ce.run()

        assert fast_ticks[0] == 6
        assert slow_ticks[0] == 3


# ------------------------------------------------------------------ #
# PN Runtime with per-node periods                                     #
# ------------------------------------------------------------------ #


class TestPnRuntimeMultiRate:
    def test_add_net_with_period(self) -> None:
        from rxnet.pn import Net, Runtime

        rt  = Runtime()
        net = Net("light", places=[1, 0], transitions=[])
        rt.add_net(net, period_us=10_000)
        rt.build()

        assert rt.period_us == 10_000
        assert rt.nslots    == 0

    def test_two_nets_different_periods(self) -> None:
        from rxnet.pn import Net, Runtime

        rt   = Runtime()
        fast = Net("fast", places=[1], transitions=[])
        slow = Net("slow", places=[1], transitions=[])
        rt.add_net(fast, period_us=10_000)
        rt.add_net(slow, period_us=20_000)
        rt.build()

        assert rt.period_us == 10_000
        assert rt.nslots    == 0
