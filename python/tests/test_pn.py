# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""Tests for rxnet.pn — equivalent to tests/test_pn.c."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import pytest

from rxnet.pn import Arc, Net, Runtime, Transition
from rxnet.runtime import Context

# ------------------------------------------------------------------ #
# Helpers                                                               #
# ------------------------------------------------------------------ #


def guard_true(ctx: Context, user: object) -> bool:
    return True


def guard_false(ctx: Context, user: object) -> bool:
    return False


g_action_calls: list[int] = []
g_action_user: list[object] = []


def counting_action(ctx: Context, user: object) -> None:
    g_action_calls.append(1)
    g_action_user.append(user)


def make_net(
    initial_places: list[int],
    transitions: list[Transition],
    user: object = None,
) -> tuple[Runtime, Net]:
    rt = Runtime()
    net = Net(
        name="test",
        places=initial_places,
        transitions=transitions,
        user=user,
    )
    rt.add_net(net)
    return rt, net


# ------------------------------------------------------------------ #
# Net lifecycle / validation                                            #
# ------------------------------------------------------------------ #


class TestNetLifecycle:
    def test_places_initialized_from_initial_list(self) -> None:
        _, net = make_net([3, 0, 1], [])
        assert net.places == [3, 0, 1]

    def test_out_of_range_place_id_in_consume_raises(self) -> None:
        with pytest.raises(IndexError):
            Net(
                name="bad",
                places=[1, 0],
                transitions=[
                    Transition(consume=[Arc(place_id=5, weight=1)])
                ],
            )

    def test_out_of_range_place_id_in_produce_raises(self) -> None:
        with pytest.raises(IndexError):
            Net(
                name="bad",
                places=[1, 0],
                transitions=[
                    Transition(produce=[Arc(place_id=99, weight=1)])
                ],
            )

    def test_negative_arc_weight_raises(self) -> None:
        with pytest.raises(ValueError):
            Net(
                name="bad",
                places=[1, 0],
                transitions=[
                    Transition(consume=[Arc(place_id=0, weight=-1)])
                ],
            )

    def test_runtime_context_accessible(self) -> None:
        rt = Runtime()
        assert rt.context is not None


# ------------------------------------------------------------------ #
# Transition firing                                                     #
# ------------------------------------------------------------------ #


class TestTransitionFiring:
    def test_transition_fires_when_tokens_available(self) -> None:
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)])],
        )
        rt.tick()
        assert net.places == [0, 1]

    def test_transition_does_not_fire_when_tokens_insufficient(self) -> None:
        rt, net = make_net(
            [0, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)])],
        )
        rt.tick()
        assert net.places == [0, 0]

    def test_transition_with_no_consume_always_fires(self) -> None:
        rt, net = make_net(
            [0],
            [Transition(produce=[Arc(0, 1)])],
        )
        rt.tick()
        assert net.places == [1]

    def test_transition_partial_tokens_does_not_fire(self) -> None:
        # Requires 2 tokens from P0, only 1 available.
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 2)], produce=[Arc(1, 1)])],
        )
        rt.tick()
        assert net.places == [1, 0]

    def test_consume_and_produce_tokens_correctly(self) -> None:
        rt, net = make_net(
            [3, 0],
            [Transition(consume=[Arc(0, 2)], produce=[Arc(1, 1)])],
        )
        rt.tick()
        assert net.places == [1, 1]


# ------------------------------------------------------------------ #
# Guards                                                                #
# ------------------------------------------------------------------ #


class TestGuards:
    def test_guard_prevents_firing_when_false(self) -> None:
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], guard=guard_false)],
        )
        rt.tick()
        assert net.places == [1, 0]

    def test_guard_allows_firing_when_true(self) -> None:
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], guard=guard_true)],
        )
        rt.tick()
        assert net.places == [0, 1]


# ------------------------------------------------------------------ #
# Actions                                                               #
# ------------------------------------------------------------------ #


class TestActions:
    def setup_method(self) -> None:
        g_action_calls.clear()
        g_action_user.clear()

    def test_action_fires_after_tick(self) -> None:
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], action=counting_action)],
        )
        rt.tick()
        assert len(g_action_calls) == 1

    def test_action_not_called_when_transition_does_not_fire(self) -> None:
        rt, net = make_net(
            [0, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], action=counting_action)],
        )
        rt.tick()
        assert len(g_action_calls) == 0

    def test_action_receives_net_user_data(self) -> None:
        sentinel = object()
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], action=counting_action)],
            user=sentinel,
        )
        rt.tick()
        assert g_action_user[0] is sentinel

    def test_action_fires_after_places_are_committed(self) -> None:
        """Action must see the committed place counts."""
        seen_places: list[list[int]] = []

        def capture(ctx: Context, user: Net) -> None:
            seen_places.append(list(user.places))

        rt = Runtime()
        net = Net(
            name="n",
            places=[1, 0],
            transitions=[Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], action=capture)],
        )
        net.user = net  # pass net itself as user so action can read places
        rt.add_net(net)
        rt.tick()

        assert seen_places == [[0, 1]]  # committed state


# ------------------------------------------------------------------ #
# Tick semantics / greedy sequential                                   #
# ------------------------------------------------------------------ #


class TestTickSemantics:
    def test_fire_flags_reset_each_tick(self) -> None:
        rt, net = make_net(
            [1, 0],
            [Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)])],
        )
        rt.tick()
        # Second tick: P0 is now 0, transition cannot fire.
        rt.tick()
        assert net.places == [0, 1]

    def test_conflicting_transitions_first_match_wins(self) -> None:
        """
        Two transitions compete for the single token in P0.
        T0 (P0→P1) and T1 (P0→P2) — only T0 should fire.
        """
        rt, net = make_net(
            [1, 0, 0],
            [
                Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)]),  # T0
                Transition(consume=[Arc(0, 1)], produce=[Arc(2, 1)]),  # T1
            ],
        )
        rt.tick()
        assert net.places == [0, 1, 0]  # T0 fired, T1 did not

    def test_second_transition_wins_when_first_blocked_by_guard(self) -> None:
        """
        T0 has false guard, T1 has none — T1 fires even though T0 was declared first.
        """
        rt, net = make_net(
            [1, 0, 0],
            [
                Transition(consume=[Arc(0, 1)], produce=[Arc(1, 1)], guard=guard_false),
                Transition(consume=[Arc(0, 1)], produce=[Arc(2, 1)]),
            ],
        )
        rt.tick()
        assert net.places == [0, 0, 1]

    def test_two_independent_transitions_both_fire(self) -> None:
        """Transitions consuming from different places can both fire."""
        rt, net = make_net(
            [1, 1, 0, 0],
            [
                Transition(consume=[Arc(0, 1)], produce=[Arc(2, 1)]),
                Transition(consume=[Arc(1, 1)], produce=[Arc(3, 1)]),
            ],
        )
        rt.tick()
        assert net.places == [0, 0, 1, 1]


# ------------------------------------------------------------------ #
# Latch / dump callbacks                                               #
# ------------------------------------------------------------------ #


class TestCallbacks:
    def test_latch_inputs_cb_called_each_tick(self) -> None:
        calls: list[int] = []

        def on_latch(ctx: Context, user: object) -> None:
            calls.append(1)

        rt = Runtime()
        net = Net(name="n", places=[1], transitions=[], latch_inputs_cb=on_latch)
        rt.add_net(net)
        rt.tick()
        rt.tick()
        assert calls == [1, 1]

    def test_dump_outputs_cb_called_each_tick(self) -> None:
        calls: list[int] = []

        def on_dump(ctx: Context, user: object) -> None:
            calls.append(1)

        rt = Runtime()
        net = Net(name="n", places=[1], transitions=[], dump_outputs_cb=on_dump)
        rt.add_net(net)
        rt.tick()
        rt.tick()
        assert calls == [1, 1]

    def test_latch_cb_runs_before_evaluate(self) -> None:
        """Tokens added in latch_inputs_cb must be visible during evaluate."""
        def add_token(ctx: Context, user: Net) -> None:
            user.places[1] += 1  # add a REQUEST token

        rt = Runtime()
        # P0=1 (OFF), P1=0 (REQUEST)
        net = Net(
            name="n",
            places=[1, 0],
            transitions=[
                # Fires only if P1 has a token
                Transition(consume=[Arc(0, 1), Arc(1, 1)], produce=[Arc(0, 1)])
            ],
            latch_inputs_cb=add_token,
        )
        net.user = net
        rt.add_net(net)
        rt.tick()

        # The latch_cb added a REQUEST token before evaluate ran,
        # so the transition fired and consumed it.
        assert net.places == [1, 0]
