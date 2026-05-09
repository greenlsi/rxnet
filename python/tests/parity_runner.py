# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""parity_runner.py — Python-side conformance runner.

Runs the same fixed scenarios as ``c/tests/parity_runner.c`` and prints one
line per tick in identical format::

    <scenario_name> <tick> <key>=<value>

run_parity.sh diffs this output against the C runner's output to verify
cross-language semantic parity.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.fsm import Machine, Transition
from rxnet.fsm import Runtime as FsmRuntime
from rxnet.pn import Arc, Net
from rxnet.pn import Runtime as PnRuntime
from rxnet.pn import Transition as PnTransition
from rxnet.runtime import Context

# ---------------------------------------------------------------------------
# Scenario: fsm_light
# FSM toggle (OFF=0, ON=1), 6 ticks: press no press no press no
# ---------------------------------------------------------------------------

def run_fsm_light() -> None:
    button: list[int] = [0]

    def latch(ctx: Context, _user: object) -> None:
        pass  # button already set via cell before tick

    def guard(ctx: Context, _user: object) -> bool:
        return bool(button[0])

    transitions = [
        Transition(0, 1, guard=guard),
        Transition(1, 0, guard=guard),
    ]
    machine = Machine("light", state=0, transitions=transitions, latch_inputs_cb=latch)
    rt = FsmRuntime()
    rt.add_machine(machine)

    for i, btn in enumerate([1, 0, 1, 0, 1, 0], start=1):
        button[0] = btn
        rt.tick()
        print(f"fsm_light {i} state={machine.state}")


# ---------------------------------------------------------------------------
# Scenario: fsm_first_match
# Two unconditional transitions from state 0: first (→1) must win over second (→2).
# ---------------------------------------------------------------------------

def run_fsm_first_match() -> None:
    transitions = [
        Transition(0, 1),   # A→B unconditional
        Transition(0, 2),   # A→C unconditional — must NOT fire
    ]
    machine = Machine("first_match", state=0, transitions=transitions)
    rt = FsmRuntime()
    rt.add_machine(machine)

    for i in range(1, 4):
        rt.tick()
        print(f"fsm_first_match {i} state={machine.state}")


# ---------------------------------------------------------------------------
# Scenario: pn_light
# PN toggle (P_OFF=0, P_ON=1, P_REQUEST=2), same 6-tick input sequence.
# ---------------------------------------------------------------------------

def run_pn_light() -> None:
    P_OFF, P_ON, P_REQUEST = 0, 1, 2
    button: list[int] = [0]

    transitions = [
        PnTransition(consume=[Arc(P_OFF, 1), Arc(P_REQUEST, 1)], produce=[Arc(P_ON, 1)]),
        PnTransition(consume=[Arc(P_ON, 1),  Arc(P_REQUEST, 1)], produce=[Arc(P_OFF, 1)]),
    ]
    net = Net("light", places=[1, 0, 0], transitions=transitions)

    def latch(ctx: Context, _user: object) -> None:
        if button[0]:
            net.places[P_REQUEST] += 1

    net.latch_inputs_cb = latch

    rt = PnRuntime()
    rt.add_net(net)

    for i, btn in enumerate([1, 0, 1, 0, 1, 0], start=1):
        button[0] = btn
        rt.tick()
        print(f"pn_light {i} p_on={net.places[P_ON]}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    run_fsm_light()
    run_fsm_first_match()
    run_pn_light()
