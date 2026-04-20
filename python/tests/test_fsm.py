# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""Tests for rxnet.fsm — equivalent to tests/test_fsm.c."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.fsm import Machine, Runtime, Transition
from rxnet.runtime import Context

STATE_A = 0
STATE_B = 1
STATE_C = 2


# ------------------------------------------------------------------ #
# Guard / action helpers                                               #
# ------------------------------------------------------------------ #


def guard_true(ctx: Context, user: object) -> bool:
    return True


def guard_false(ctx: Context, user: object) -> bool:
    return False


def guard_flag(ctx: Context, user: list[int]) -> bool:
    return bool(user[0])


g_action_calls: list[int] = []
g_action_user: list[object] = []


def counting_action(ctx: Context, user: object) -> None:
    g_action_calls.append(1)
    g_action_user.append(user)


# ------------------------------------------------------------------ #
# Helper: build a runtime + machine in one call                        #
# ------------------------------------------------------------------ #


def make_machine(
    initial_state: int,
    transitions: list[Transition],
    user: object = None,
) -> tuple[Runtime, Machine]:
    rt = Runtime()
    machine = Machine(
        name="test",
        state=initial_state,
        transitions=transitions,
        user=user,
    )
    rt.add_machine(machine)
    return rt, machine


# ------------------------------------------------------------------ #
# Lifecycle                                                             #
# ------------------------------------------------------------------ #


class TestLifecycle:
    def test_machine_starts_in_initial_state(self) -> None:
        m = Machine(name="m", state=STATE_B, transitions=[])
        assert m.state == STATE_B

    def test_runtime_context_accessible(self) -> None:
        rt = Runtime()
        assert rt.context is not None

    def test_add_machine_appears_in_machines(self) -> None:
        rt = Runtime()
        m = Machine(name="m", state=0, transitions=[])
        rt.add_machine(m)
        assert m in rt.machines


# ------------------------------------------------------------------ #
# State transitions                                                     #
# ------------------------------------------------------------------ #


class TestTransitions:
    def test_no_guard_always_transitions(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, guard=None, action=None)],
        )
        rt.tick()
        assert m.state == STATE_B

    def test_true_guard_transitions(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, guard=guard_true)],
        )
        rt.tick()
        assert m.state == STATE_B

    def test_false_guard_stays_in_state(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, guard=guard_false)],
        )
        rt.tick()
        assert m.state == STATE_A

    def test_no_matching_from_state_stays_in_state(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_B, STATE_C, guard=None)],
        )
        rt.tick()
        assert m.state == STATE_A

    def test_first_match_in_declaration_order_wins(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [
                Transition(STATE_A, STATE_B, guard=None),
                Transition(STATE_A, STATE_C, guard=None),
            ],
        )
        rt.tick()
        assert m.state == STATE_B

    def test_skips_false_guard_takes_second_match(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [
                Transition(STATE_A, STATE_B, guard=guard_false),
                Transition(STATE_A, STATE_C, guard=None),
            ],
        )
        rt.tick()
        assert m.state == STATE_C

    def test_guard_reads_user_data(self) -> None:
        flag = [1]
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, guard=guard_flag)],
            user=flag,
        )
        rt.tick()
        assert m.state == STATE_B

        flag[0] = 0
        m.state = STATE_A
        rt.tick()
        assert m.state == STATE_A


# ------------------------------------------------------------------ #
# Actions                                                               #
# ------------------------------------------------------------------ #


class TestActions:
    def setup_method(self) -> None:
        g_action_calls.clear()
        g_action_user.clear()

    def test_action_is_called_after_tick(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, action=counting_action)],
        )
        rt.tick()
        assert len(g_action_calls) == 1

    def test_action_not_called_when_no_transition(self) -> None:
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, guard=guard_false, action=counting_action)],
        )
        rt.tick()
        assert len(g_action_calls) == 0

    def test_action_receives_machine_user_data(self) -> None:
        sentinel = object()
        rt, m = make_machine(
            STATE_A,
            [Transition(STATE_A, STATE_B, action=counting_action)],
            user=sentinel,
        )
        rt.tick()
        assert g_action_user[0] is sentinel

    def test_action_fires_after_state_is_committed(self) -> None:
        """Action must see the committed (new) state, not the old one."""
        state_at_action: list[int] = []

        def capture_state(ctx: Context, user: Machine) -> None:
            state_at_action.append(user.state)

        rt = Runtime()
        m = Machine(name="m", state=STATE_A, transitions=[])
        # capture_state receives `m` as user data
        m.transitions = [Transition(STATE_A, STATE_B, action=capture_state)]
        m.user = m
        rt.add_machine(m)

        rt.tick()

        assert state_at_action == [STATE_B]


# ------------------------------------------------------------------ #
# Latch / dump callbacks                                               #
# ------------------------------------------------------------------ #


class TestCallbacks:
    def test_latch_inputs_cb_called_each_tick(self) -> None:
        calls: list[int] = []

        def on_latch(ctx: Context, user: object) -> None:
            calls.append(1)

        rt = Runtime()
        m = Machine(
            name="m",
            state=STATE_A,
            transitions=[],
            latch_inputs_cb=on_latch,
        )
        rt.add_machine(m)
        rt.tick()
        rt.tick()
        assert calls == [1, 1]

    def test_dump_outputs_cb_called_each_tick(self) -> None:
        calls: list[int] = []

        def on_dump(ctx: Context, user: object) -> None:
            calls.append(1)

        rt = Runtime()
        m = Machine(
            name="m",
            state=STATE_A,
            transitions=[],
            dump_outputs_cb=on_dump,
        )
        rt.add_machine(m)
        rt.tick()
        rt.tick()
        assert calls == [1, 1]

    def test_dump_runs_after_deferred_actions(self) -> None:
        """Dump must run after deferred actions (not before)."""
        order: list[str] = []

        def action(ctx: Context, user: object) -> None:
            order.append("action")

        def on_dump(ctx: Context, user: object) -> None:
            order.append("dump")

        rt = Runtime()
        m = Machine(
            name="m",
            state=STATE_A,
            transitions=[Transition(STATE_A, STATE_B, action=action)],
            dump_outputs_cb=on_dump,
        )
        rt.add_machine(m)
        rt.tick()

        assert order == ["action", "dump"]


# ------------------------------------------------------------------ #
# Multiple machines                                                     #
# ------------------------------------------------------------------ #


class TestMultipleMachines:
    def test_two_machines_tick_independently(self) -> None:
        trans_m1 = [Transition(STATE_A, STATE_B)]
        trans_m2 = [Transition(STATE_A, STATE_C)]

        rt = Runtime()
        m1 = Machine(name="m1", state=STATE_A, transitions=trans_m1)
        m2 = Machine(name="m2", state=STATE_A, transitions=trans_m2)
        rt.add_machine(m1)
        rt.add_machine(m2)

        rt.tick()

        assert m1.state == STATE_B
        assert m2.state == STATE_C
