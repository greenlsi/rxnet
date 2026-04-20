# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""Tests for rxnet.trace and rxnet.diagram."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.diagram import fsm_to_dot, pn_to_dot
from rxnet.fsm import Machine
from rxnet.fsm import Runtime as FsmRuntime
from rxnet.fsm import Transition as FsmTransition
from rxnet.pn import Arc, Net
from rxnet.pn import Runtime as PnRuntime
from rxnet.pn import Transition as PnTransition
from rxnet.trace import (
    _EV_FSM,
    _EV_N_END,
    _EV_N_START,
    _EV_PH_END,
    _EV_PH_START,
    _EV_PN,
    _EV_USER,
    _PACK,
    Tracer,
    _RingBuf,
)

# ── fixtures ──────────────────────────────────────────────────────────────────

OFF, ON = 0, 1


def _flag_machine(flag: list[bool]) -> tuple[Machine, FsmRuntime]:
    """Two-state FSM (OFF→ON when flag[0], ON→OFF always)."""
    machine = Machine(
        name="light",
        state=OFF,
        state_names={OFF: "OFF", ON: "ON"},
        transitions=[
            FsmTransition(from_state=OFF, to_state=ON,
                          guard=lambda ctx, u: u[0], label="press"),
            FsmTransition(from_state=ON,  to_state=OFF,
                          guard=lambda ctx, u: True, label="release"),
        ],
        user=flag,
    )
    rt = FsmRuntime()
    rt.add_machine(machine)
    rt.build()
    return machine, rt


def _simple_pn() -> tuple[Net, PnRuntime]:
    """Two-place, one-transition PN: P0→T0→P1."""
    net = Net(
        name="flow",
        places=[1, 0],
        place_names={0: "SRC", 1: "DST"},
        transition_names=["move"],
        transitions=[
            PnTransition(consume=[Arc(0)], produce=[Arc(1)], label="move"),
        ],
    )
    rt = PnRuntime()
    rt.add_net(net)
    rt.build()
    return net, rt


# ── _RingBuf ─────────────────────────────────────────────────────────────────

class TestRingBuf:
    def test_empty_drain(self):
        buf = _RingBuf(16)
        data, n, dropped, _ = buf.drain()
        assert n == 0
        assert dropped == 0
        assert data == b""

    def test_single_write_drain(self):
        buf = _RingBuf(16)
        buf.write(1, 2, a=3, b=4, c=5)
        data, n, dropped, _ = buf.drain()
        assert n == 1
        assert dropped == 0
        t_ns, kind, nid, a, b, c = _PACK.unpack_from(data, 0)
        assert kind == 1
        assert nid == 2
        assert a == 3
        assert b == 4
        assert c == 5

    def test_events_in_order(self):
        buf = _RingBuf(16)
        for i in range(5):
            buf.write(i, 0)
        data, n, dropped, _ = buf.drain()
        assert n == 5
        kinds = [_PACK.unpack_from(data, i * 16)[1] for i in range(5)]
        assert kinds == list(range(5))

    def test_overflow_counts_dropped(self):
        cap = 4
        buf = _RingBuf(cap)
        for i in range(cap + 2):
            buf.write(0, 0)
        _, n, dropped, _ = buf.drain()
        assert n == cap
        assert dropped == 2

    def test_overflow_keeps_newest_events(self):
        """After overflow the newest events are returned, oldest dropped."""
        cap = 4
        buf = _RingBuf(cap)
        for i in range(cap + 1):
            buf.write(i, 0)          # kind = i
        data, n, dropped, _ = buf.drain()
        assert n == cap
        assert dropped == 1
        kinds = [_PACK.unpack_from(data, i * 16)[1] for i in range(n)]
        # The oldest event (kind=0) was dropped; we should see 1,2,3,4
        assert kinds == [1, 2, 3, 4]

    def test_drain_is_repeatable(self):
        """drain() does not clear the buffer — same data on second call."""
        buf = _RingBuf(16)
        buf.write(7, 3)
        _, n1, _, _ = buf.drain()
        _, n2, _, _ = buf.drain()
        assert n1 == n2 == 1


# ── Tracer.attach / detach ───────────────────────────────────────────────────

class TestTracerAttachDetach:
    def test_attach_wraps_nodes(self):
        from rxnet.trace import _Traced
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        core = tracer._core(rt)
        assert all(isinstance(e.node, _Traced) for e in core._entries)

    def test_detach_restores_nodes(self):
        from rxnet.trace import _Traced
        machine, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        tracer.detach(rt)
        core = tracer._core(rt)
        assert not any(isinstance(e.node, _Traced) for e in core._entries)
        # original machine is back
        assert core._entries[0].node is machine

    def test_attach_snapshots_initial_state(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        assert tracer._inits[0] == OFF

    def test_attach_records_name(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        assert tracer._names[0] == "light"


# ── FSM event recording ───────────────────────────────────────────────────────

class TestFsmEvents:
    def _run_tick(self, flag_val: bool, phases: bool = False):
        flag = [flag_val]
        machine, rt = _flag_machine(flag)
        tracer = Tracer(phases=phases)
        tracer.attach(rt)
        rt.tick()
        data, n, _, _ = tracer._buf.drain()
        return [_PACK.unpack_from(data, i * 16) for i in range(n)]

    def test_n_start_and_end_always_recorded(self):
        evs = self._run_tick(False)
        kinds = [e[1] for e in evs]
        assert _EV_N_START in kinds
        assert _EV_N_END in kinds

    def test_no_fsm_event_when_no_transition(self):
        # OFF with flag=False: guard fails, stays OFF
        evs = self._run_tick(False)
        kinds = [e[1] for e in evs]
        assert _EV_FSM not in kinds

    def test_fsm_event_recorded_on_transition(self):
        # OFF→ON when flag=True
        evs = self._run_tick(True)
        kinds = [e[1] for e in evs]
        assert _EV_FSM in kinds

    def test_fsm_event_carries_from_to(self):
        evs = self._run_tick(True)
        fsm_evs = [e for e in evs if e[1] == _EV_FSM]
        assert len(fsm_evs) == 1
        _, _, nid, from_s, to_s, _ = fsm_evs[0]
        assert from_s == OFF
        assert to_s == ON

    def test_phase_events_recorded_when_enabled(self):
        evs = self._run_tick(False, phases=True)
        kinds = [e[1] for e in evs]
        assert _EV_PH_START in kinds
        assert _EV_PH_END in kinds

    def test_no_phase_events_when_disabled(self):
        evs = self._run_tick(False, phases=False)
        kinds = [e[1] for e in evs]
        assert _EV_PH_START not in kinds
        assert _EV_PH_END not in kinds

    def test_n_start_before_n_end(self):
        evs = self._run_tick(False)
        kinds = [e[1] for e in evs]
        assert kinds.index(_EV_N_START) < kinds.index(_EV_N_END)

    def test_fsm_event_between_start_and_end(self):
        evs = self._run_tick(True)
        kinds = [e[1] for e in evs]
        assert kinds.index(_EV_N_START) < kinds.index(_EV_FSM) < kinds.index(_EV_N_END)


# ── PN event recording ────────────────────────────────────────────────────────

class TestPnEvents:
    def _run_tick(self):
        net, rt = _simple_pn()
        tracer = Tracer()
        tracer.attach(rt)
        rt.tick()
        data, n, _, _ = tracer._buf.drain()
        return [_PACK.unpack_from(data, i * 16) for i in range(n)]

    def test_pn_event_recorded_on_firing(self):
        evs = self._run_tick()
        kinds = [e[1] for e in evs]
        assert _EV_PN in kinds

    def test_pn_event_carries_transition_index(self):
        evs = self._run_tick()
        pn_evs = [e for e in evs if e[1] == _EV_PN]
        assert len(pn_evs) == 1
        _, _, _, tid, _, _ = pn_evs[0]
        assert tid == 0   # first transition

    def test_no_pn_event_when_disabled(self):
        """Second tick: P0 now has 0 tokens, transition cannot fire."""
        net, rt = _simple_pn()
        tracer = Tracer()
        tracer.attach(rt)
        rt.tick()  # fires
        rt.tick()  # cannot fire (P0 = 0)
        data, n, _, _ = tracer._buf.drain()
        evs = [_PACK.unpack_from(data, i * 16) for i in range(n)]
        pn_tick2 = [e for e in evs if e[1] == _EV_PN]
        # second tick produced no PN events (only the first tick's firing matters)
        # We check by looking at how many N_START events there are (should be 2)
        # and only one PN event total
        assert len(pn_tick2) == 1   # only from the first tick


# ── user events ───────────────────────────────────────────────────────────────

class TestUserEvents:
    def test_user_event_recorded(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        tracer.user("sensor", 42)
        data, n, _, _ = tracer._buf.drain()
        evs = [_PACK.unpack_from(data, i * 16) for i in range(n)]
        user_evs = [e for e in evs if e[1] == _EV_USER]
        assert len(user_evs) == 1
        _, _, _, lid, val, _ = user_evs[0]
        assert val == 42
        assert tracer._labels["sensor"] == lid

    def test_user_label_reused(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        tracer.user("x", 1)
        tracer.user("x", 2)
        assert len(tracer._labels) == 1   # only one label registered

    def test_user_value_masked_to_16_bits(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        tracer.user("big", 0x1FFFF)
        data, n, _, _ = tracer._buf.drain()
        evs = [_PACK.unpack_from(data, i * 16) for i in range(n)]
        user_evs = [e for e in evs if e[1] == _EV_USER]
        _, _, _, _, val, _ = user_evs[0]
        assert val == 0xFFFF


# ── binary export ─────────────────────────────────────────────────────────────

class TestBinaryExport:
    def test_blob_magic(self):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        blob = tracer._blob()
        assert blob[:4] == b"RXNT"

    def test_blob_parseable_by_tool(self):
        """The tools/trace.py _Trace parser should accept the blob."""
        from rxnet.tools.trace import _Trace
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        rt.tick()
        blob = tracer._blob()
        t = _Trace(blob)
        assert t.n_ev >= 2   # at least N_START + N_END
        assert t.lang == "P"

    def test_name_table_round_trip(self):
        from rxnet.tools.trace import _Trace
        machine, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        blob = tracer._blob()
        t = _Trace(blob)
        assert t.node_name(0) == "light"
        assert t.state_name(0, OFF) == "OFF"
        assert t.state_name(0, ON)  == "ON"

    def test_pn_name_table_round_trip(self):
        from rxnet.tools.trace import _Trace
        net, rt = _simple_pn()
        tracer = Tracer()
        tracer.attach(rt)
        blob = tracer._blob()
        t = _Trace(blob)
        assert t.node_name(0) == "flow"

    def test_export_writes_file(self, tmp_path):
        _, rt = _flag_machine([False])
        tracer = Tracer()
        tracer.attach(rt)
        out = str(tmp_path / "trace.bin")
        tracer.export(out)
        data = Path(out).read_bytes()
        assert data[:4] == b"RXNT"

    def test_phases_flag_in_header(self):
        from rxnet.tools.trace import _Trace
        _, rt = _flag_machine([False])
        tracer = Tracer(phases=True)
        tracer.attach(rt)
        blob = tracer._blob()
        t = _Trace(blob)
        assert t.phases is True

    def test_no_phases_flag_in_header(self):
        from rxnet.tools.trace import _Trace
        _, rt = _flag_machine([False])
        tracer = Tracer(phases=False)
        tracer.attach(rt)
        blob = tracer._blob()
        t = _Trace(blob)
        assert t.phases is False


# ── diagram generation ────────────────────────────────────────────────────────

class TestFsmDot:
    def _machine(self) -> Machine:
        return Machine(
            name="light",
            state=OFF,
            state_names={OFF: "OFF", ON: "ON"},
            transitions=[
                FsmTransition(from_state=OFF, to_state=ON,  label="press"),
                FsmTransition(from_state=ON,  to_state=OFF, label="release"),
            ],
        )

    def test_returns_string(self):
        dot = fsm_to_dot(self._machine())
        assert isinstance(dot, str)

    def test_digraph_keyword(self):
        dot = fsm_to_dot(self._machine())
        assert "digraph" in dot

    def test_state_names_present(self):
        dot = fsm_to_dot(self._machine())
        assert "OFF" in dot
        assert "ON" in dot

    def test_initial_state_arrow(self):
        dot = fsm_to_dot(self._machine())
        assert "__start" in dot

    def test_arc_labels_present(self):
        dot = fsm_to_dot(self._machine())
        assert "press" in dot
        assert "release" in dot

    def test_custom_initial_state(self):
        m = self._machine()
        dot = fsm_to_dot(m, initial_state=ON)
        # The initial arrow should point to ON
        lines = dot.splitlines()
        start_arrow = next(ln for ln in lines if "__start ->" in ln)
        assert "ON" in start_arrow

    def test_no_state_names_fallback(self):
        m = Machine(name="m", state=0,
                    transitions=[FsmTransition(from_state=0, to_state=1)])
        dot = fsm_to_dot(m)
        assert "s0" in dot
        assert "s1" in dot


class TestPnDot:
    def _net(self) -> Net:
        return Net(
            name="flow",
            places=[1, 0],
            place_names={0: "SRC", 1: "DST"},
            transition_names=["move"],
            transitions=[
                PnTransition(consume=[Arc(0)], produce=[Arc(1)], label="move"),
            ],
        )

    def test_returns_string(self):
        dot = pn_to_dot(self._net())
        assert isinstance(dot, str)

    def test_digraph_keyword(self):
        dot = pn_to_dot(self._net())
        assert "digraph" in dot

    def test_place_names_present(self):
        dot = pn_to_dot(self._net())
        assert "SRC" in dot
        assert "DST" in dot

    def test_transition_names_present(self):
        dot = pn_to_dot(self._net())
        assert "move" in dot

    def test_token_mark_for_initial_tokens(self):
        dot = pn_to_dot(self._net())
        # SRC has 1 token so should have a bullet mark
        assert "●" in dot

    def test_arcs_present(self):
        dot = pn_to_dot(self._net())
        assert "->" in dot

    def test_weighted_arc_label(self):
        net = Net(
            name="w",
            places=[2, 0],
            transitions=[
                PnTransition(consume=[Arc(0, weight=2)], produce=[Arc(1)]),
            ],
        )
        dot = pn_to_dot(net)
        assert '"2"' in dot or "2" in dot

    def test_no_names_fallback(self):
        net = Net(name="n", places=[1, 0],
                  transitions=[PnTransition(consume=[Arc(0)], produce=[Arc(1)])])
        dot = pn_to_dot(net)
        assert "P0" in dot
        assert "T0" in dot
