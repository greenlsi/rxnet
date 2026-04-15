"""Tests for rxnet.runtime — equivalent to tests/test_runtime.c."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import pytest
from rxnet.runtime import Context, DeferredAction, Runtime


# ------------------------------------------------------------------ #
# Minimal Node implementation for testing                              #
# ------------------------------------------------------------------ #


class TrackingNode:
    """Records how many times each phase was called and in what order."""

    def __init__(self, phase_id: int, order_log: list[int]) -> None:
        self.phase_id = phase_id
        self.order_log = order_log
        self.latch_calls = 0
        self.eval_calls = 0
        self.commit_calls = 0
        self.dump_calls = 0

    def latch_inputs(self, ctx: Context) -> None:
        self.latch_calls += 1
        self.order_log.append(self.phase_id * 10 + 0)

    def evaluate(self, ctx: Context) -> None:
        self.eval_calls += 1
        self.order_log.append(self.phase_id * 10 + 1)

    def commit(self, ctx: Context) -> None:
        self.commit_calls += 1
        self.order_log.append(self.phase_id * 10 + 2)

    def dump_outputs(self, ctx: Context) -> None:
        self.dump_calls += 1
        self.order_log.append(self.phase_id * 10 + 3)


# ------------------------------------------------------------------ #
# Context tests                                                         #
# ------------------------------------------------------------------ #


class TestContext:
    def test_init_no_inputs(self) -> None:
        ctx = Context()
        assert ctx.inputs is None
        assert ctx.latched_inputs is None

    def test_init_with_inputs(self) -> None:
        ctx = Context(inputs=42)
        assert ctx.inputs == 42

    def test_latch_inputs_copies(self) -> None:
        ctx = Context(inputs=[1, 2, 3])
        ctx.inputs[0] = 99
        ctx.latch_inputs()
        assert ctx.latched_inputs[0] == 99

    def test_latch_is_shallow_copy(self) -> None:
        ctx = Context(inputs=[1, 2, 3])
        original = ctx.inputs
        ctx.latch_inputs()
        assert ctx.latched_inputs is not original
        assert ctx.latched_inputs == original

    def test_enqueue_null_function_raises(self) -> None:
        ctx = Context()
        with pytest.raises((TypeError, AttributeError)):
            ctx.enqueue_deferred_action(None, None)  # type: ignore

    def test_enqueue_single_action_executes_on_run(self) -> None:
        ctx = Context()
        calls: list[int] = []

        def action(c: Context, user: object) -> None:
            calls.append(1)

        ctx.enqueue_deferred_action(action, None)
        ctx.run_deferred_actions()
        assert calls == [1]

    def test_enqueue_passes_user_data_to_callback(self) -> None:
        ctx = Context()
        received: list[object] = []
        sentinel = object()

        def action(c: Context, user: object) -> None:
            received.append(user)

        ctx.enqueue_deferred_action(action, sentinel)
        ctx.run_deferred_actions()
        assert received[0] is sentinel

    def test_enqueue_multiple_actions_run_in_fifo_order(self) -> None:
        ctx = Context()
        order: list[str] = []

        ctx.enqueue_deferred_action(lambda c, u: order.append("A"), None)
        ctx.enqueue_deferred_action(lambda c, u: order.append("B"), None)
        ctx.run_deferred_actions()
        assert order == ["A", "B"]

    def test_run_deferred_actions_clears_queue(self) -> None:
        ctx = Context()
        calls: list[int] = []
        ctx.enqueue_deferred_action(lambda c, u: calls.append(1), None)
        ctx.run_deferred_actions()
        ctx.run_deferred_actions()  # second run should do nothing
        assert calls == [1]


# ------------------------------------------------------------------ #
# Runtime tests                                                         #
# ------------------------------------------------------------------ #


class TestRuntime:
    def test_add_node_stores_node(self) -> None:
        rt = Runtime()
        node = TrackingNode(1, [])
        rt.add_node(node)
        assert node in rt.nodes

    def test_tick_calls_all_phases(self) -> None:
        rt = Runtime()
        order: list[int] = []
        node = TrackingNode(1, order)
        rt.add_node(node)

        rt.tick()

        assert node.latch_calls == 1
        assert node.eval_calls == 1
        assert node.commit_calls == 1
        assert node.dump_calls == 1

    def test_tick_phases_execute_in_correct_order_single_node(self) -> None:
        rt = Runtime()
        order: list[int] = []
        node = TrackingNode(1, order)
        rt.add_node(node)

        rt.tick()

        # latch(10) → eval(11) → commit(12) → dump(13)
        assert order == [10, 11, 12, 13]

    def test_tick_phases_execute_in_correct_order_two_nodes(self) -> None:
        """
        For two nodes A(id=1) and B(id=2):
          latch_A, latch_B, eval_A, eval_B, commit_A, commit_B, dump_A, dump_B
        """
        rt = Runtime()
        order: list[int] = []
        a = TrackingNode(1, order)
        b = TrackingNode(2, order)
        rt.add_node(a)
        rt.add_node(b)

        rt.tick()

        assert order == [10, 20, 11, 21, 12, 22, 13, 23]

    def test_deferred_actions_run_after_all_commits_before_dump(self) -> None:
        """Action enqueued during commit must run before dump_outputs."""
        rt = Runtime()
        order: list[int] = []

        class ActionNode:
            def latch_inputs(self, ctx: Context) -> None:
                pass

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                ctx.enqueue_deferred_action(
                    lambda c, u: order.append("action"), None
                )

            def dump_outputs(self, ctx: Context) -> None:
                order.append("dump")

        rt.add_node(ActionNode())
        rt.tick()

        assert order == ["action", "dump"]

    def test_global_latch_inputs_called_before_per_node(self) -> None:
        """context.latch_inputs() runs before any node.latch_inputs()."""

        class SimpleInputs:
            def __init__(self) -> None:
                self.value = 0

        inputs = SimpleInputs()
        rt = Runtime(inputs=inputs)
        seen_in_latch: list[int] = []

        class ObserverNode:
            def latch_inputs(self, ctx: Context) -> None:
                # By the time per-node latch runs, latched_inputs should already
                # be a copy of inputs (global latch ran first).
                seen_in_latch.append(ctx.latched_inputs.value)

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                pass

            def dump_outputs(self, ctx: Context) -> None:
                pass

        rt.add_node(ObserverNode())
        inputs.value = 7
        rt.tick()

        assert seen_in_latch == [7]
