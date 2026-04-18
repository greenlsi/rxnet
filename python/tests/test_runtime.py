"""Tests for rxnet.runtime — equivalent to tests/test_runtime.c."""
from __future__ import annotations

import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import pytest

from rxnet.runtime import Context, Runtime
from rxnet.worker_pool import Priority, WorkerPool

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


# ------------------------------------------------------------------ #
# Priority deferred tests                                              #
# ------------------------------------------------------------------ #


class TestDeferredPriority:
    def test_high_priority_action_runs_before_low(self) -> None:
        ctx   = Context()
        order: list[str] = []

        ctx.enqueue_deferred_action(lambda c, u: order.append("low"),  None, Priority.LOW)
        ctx.enqueue_deferred_action(lambda c, u: order.append("high"), None, Priority.HIGH)
        ctx.run_deferred_actions()

        assert order == ["high", "low"]

    def test_fifo_within_same_priority(self) -> None:
        ctx   = Context()
        order: list[str] = []

        ctx.enqueue_deferred_action(lambda c, u: order.append("A"), None, Priority.NORMAL)
        ctx.enqueue_deferred_action(lambda c, u: order.append("B"), None, Priority.NORMAL)
        ctx.enqueue_deferred_action(lambda c, u: order.append("C"), None, Priority.NORMAL)
        ctx.run_deferred_actions()

        assert order == ["A", "B", "C"]

    def test_mixed_priorities_order(self) -> None:
        ctx   = Context()
        order: list[str] = []

        ctx.enqueue_deferred_action(lambda c, u: order.append("normal"), None, Priority.NORMAL)
        ctx.enqueue_deferred_action(lambda c, u: order.append("low"),    None, Priority.LOW)
        ctx.enqueue_deferred_action(lambda c, u: order.append("critical"),None, Priority.CRITICAL)
        ctx.enqueue_deferred_action(lambda c, u: order.append("high"),   None, Priority.HIGH)
        ctx.run_deferred_actions()

        assert order == ["critical", "high", "normal", "low"]

    def test_enqueue_thread_safe(self) -> None:
        """Concurrent enqueue from multiple threads must not lose actions."""
        ctx     = Context()
        n       = 100
        barrier = threading.Barrier(n)

        def enqueue(_: object) -> None:
            barrier.wait()
            ctx.enqueue_deferred_action(lambda c, u: None, None)

        threads = [threading.Thread(target=enqueue, args=(None,)) for _ in range(n)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        with ctx._deferred_lock:
            assert len(ctx._deferred_actions) == n


# ------------------------------------------------------------------ #
# Parallel tick tests                                                  #
# ------------------------------------------------------------------ #


class TestParallelTick:
    def test_parallel_tick_calls_all_phases(self) -> None:
        with ThreadPoolExecutor(max_workers=4) as ex:
            rt   = Runtime(executor=ex)
            node = TrackingNode(1, [])
            rt.add_node(node)
            rt.tick()

        assert node.latch_calls == 1
        assert node.eval_calls  == 1
        assert node.commit_calls == 1
        assert node.dump_calls  == 1

    def test_all_nodes_latch_before_any_evaluate(self) -> None:
        """All latch phases must finish before any evaluate phase starts."""
        # Each node records a timestamp at latch and evaluate.
        # We verify: max(latch_times) < min(eval_times).
        import time

        latch_times: list[float] = []
        eval_times:  list[float] = []
        lock = threading.Lock()

        class TimedNode:
            def latch_inputs(self, ctx: Context) -> None:
                time.sleep(0.01)           # stagger latches
                with lock:
                    latch_times.append(time.monotonic())

            def evaluate(self, ctx: Context) -> None:
                with lock:
                    eval_times.append(time.monotonic())

            def commit(self, ctx: Context) -> None:
                pass

            def dump_outputs(self, ctx: Context) -> None:
                pass

        with ThreadPoolExecutor(max_workers=4) as ex:
            rt = Runtime(executor=ex)
            for _ in range(4):
                rt.add_node(TimedNode())
            rt.tick()

        assert max(latch_times) < min(eval_times), (
            f"Some evaluate started before all latches finished: "
            f"last_latch={max(latch_times):.4f}, first_eval={min(eval_times):.4f}"
        )

    def test_all_nodes_commit_before_any_dump(self) -> None:
        """All commit phases must finish before any dump phase starts."""
        import time

        commit_times: list[float] = []
        dump_times:   list[float] = []
        lock = threading.Lock()

        class TimedNode:
            def latch_inputs(self, ctx: Context) -> None:
                pass

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                time.sleep(0.01)
                with lock:
                    commit_times.append(time.monotonic())

            def dump_outputs(self, ctx: Context) -> None:
                with lock:
                    dump_times.append(time.monotonic())

        with ThreadPoolExecutor(max_workers=4) as ex:
            rt = Runtime(executor=ex)
            for _ in range(4):
                rt.add_node(TimedNode())
            rt.tick()

        assert max(commit_times) < min(dump_times), (
            f"Some dump started before all commits finished: "
            f"last_commit={max(commit_times):.4f}, first_dump={min(dump_times):.4f}"
        )

    def test_parallel_and_sequential_produce_same_fsm_result(self) -> None:
        """Parallel tick must produce identical state transitions as sequential."""
        from rxnet.fsm import Machine, Transition
        from rxnet.fsm import Runtime as FsmRuntime

        button: list[bool] = [False]

        def guard(ctx: Context, _: object) -> bool:
            return button[0]

        transitions = [Transition(0, 1, guard=guard), Transition(1, 0, guard=guard)]

        def make_machine() -> Machine:
            return Machine("light", state=0, transitions=transitions)

        # Sequential
        seq_rt = FsmRuntime()
        seq_m  = make_machine()
        seq_rt.add_machine(seq_m)

        # Parallel
        with ThreadPoolExecutor(max_workers=2) as ex:
            from rxnet.runtime import Runtime as CoreRuntime
            par_rt = CoreRuntime(executor=ex)
            par_m  = make_machine()
            par_rt.add_node(par_m)

            for btn in [True, False, True, False, True]:
                button[0] = btn
                seq_rt.tick()
                par_rt.tick()
                assert seq_m.state == par_m.state


# ------------------------------------------------------------------ #
# Async deferred tests                                                 #
# ------------------------------------------------------------------ #


class TestAsyncDeferred:
    def test_async_deferred_posts_to_pool(self) -> None:
        """With a worker_pool, dispatch_deferred must not run actions inline."""
        ran_inline = threading.Event()
        ran_in_pool: list[threading.Thread] = []

        def action(ctx: Context, user: object) -> None:
            ran_in_pool.append(threading.current_thread())

        ctx = Context()
        ctx.enqueue_deferred_action(action, None)

        with WorkerPool(num_workers=1) as pool:
            ctx.dispatch_deferred(worker_pool=pool)
            # Give the pool time to execute.
            done = threading.Event()
            pool.submit(lambda c, u: done.set(), None, None)
            assert done.wait(timeout=2.0)

        # Action ran in a worker thread, not the calling thread.
        assert len(ran_in_pool) == 1
        assert ran_in_pool[0] is not threading.main_thread()

    def test_async_deferred_respects_priority(self) -> None:
        """Higher-priority actions submitted to the pool run first."""
        gate  = threading.Event()
        order: list[str] = []
        done  = threading.Event()

        def low(ctx: Context, user: object) -> None:
            gate.wait()
            order.append("low")

        def high(ctx: Context, user: object) -> None:
            gate.wait()
            order.append("high")
            done.set()

        ctx = Context()
        ctx.enqueue_deferred_action(low,  None, Priority.LOW)
        ctx.enqueue_deferred_action(high, None, Priority.HIGH)

        with WorkerPool(num_workers=1) as pool:
            ctx.dispatch_deferred(worker_pool=pool)
            gate.set()
            assert done.wait(timeout=2.0)

        assert order == ["high", "low"]

    def test_runtime_with_async_deferred_tick_does_not_block(self) -> None:
        """tick() should return before slow async actions complete."""
        import time

        slow_started = threading.Event()
        slow_done    = threading.Event()

        class SlowNode:
            def latch_inputs(self, ctx: Context) -> None:
                pass

            def evaluate(self, ctx: Context) -> None:
                pass

            def commit(self, ctx: Context) -> None:
                def slow_action(c: Context, u: object) -> None:
                    slow_started.set()
                    time.sleep(0.1)
                    slow_done.set()

                ctx.enqueue_deferred_action(slow_action, None)

            def dump_outputs(self, ctx: Context) -> None:
                pass

        with WorkerPool(num_workers=1) as pool:
            rt = Runtime(worker_pool=pool)
            rt.add_node(SlowNode())

            t0 = time.monotonic()
            rt.tick()
            elapsed = time.monotonic() - t0

            # tick() must return well before the 100 ms action finishes.
            assert elapsed < 0.05, f"tick() blocked on slow action ({elapsed:.3f}s)"
            assert slow_done.wait(timeout=2.0), "slow action never completed"
