# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

import ast
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.tools.extract_py import (
    _collect_enum_constants,
    _find_machine_init,
    _find_transitions,
    ast_to_spec,
)

LIGHT_PY = """\
LIGHT_STATE_OFF = 0
LIGHT_STATE_ON = 1

_TRANSITIONS = [
    Transition(LIGHT_STATE_OFF, LIGHT_STATE_ON, guard=_button_pressed, action=_light_on),
    Transition(LIGHT_STATE_ON, LIGHT_STATE_OFF, guard=_button_pressed, action=_light_off),
]

def create_light_fsm(button_gpio: int, light_gpio: int):
    return Machine(
        name="light",
        state=LIGHT_STATE_OFF,
        transitions=_TRANSITIONS,
    )
"""

_TREE = ast.parse(LIGHT_PY)


def test_collect_enum_constants():
    result = _collect_enum_constants(_TREE)
    assert result == {"LIGHT_STATE_OFF": 0, "LIGHT_STATE_ON": 1}


def test_find_transitions():
    pairs = _find_transitions(_TREE)
    assert pairs == [
        ("LIGHT_STATE_OFF", "LIGHT_STATE_ON", "button_pressed", "light_on"),
        ("LIGHT_STATE_ON", "LIGHT_STATE_OFF", "button_pressed", "light_off"),
    ]


def test_find_transitions_strips_underscore():
    src = """\
LIGHT_STATE_OFF = 0
LIGHT_STATE_ON = 1
_T = [Transition(LIGHT_STATE_OFF, LIGHT_STATE_ON, guard=__my_guard, action=_my_action)]
"""
    tree = ast.parse(src)
    pairs = _find_transitions(tree)
    assert pairs[0][2] == "my_guard"
    assert pairs[0][3] == "my_action"


def test_find_machine_init():
    result = _find_machine_init(_TREE)
    assert result == ("light", "LIGHT_STATE_OFF")


def test_find_machine_init_none():
    tree = ast.parse("x = 1")
    assert _find_machine_init(tree) is None


def test_ast_to_spec_states():
    spec = ast_to_spec(_TREE)
    assert spec.name == "light"
    assert len(spec.states) == 2
    assert spec.states[0].name == "OFF"
    assert spec.states[0].id == 0
    assert spec.states[1].name == "ON"
    assert spec.states[1].id == 1


def test_ast_to_spec_initial():
    spec = ast_to_spec(_TREE)
    assert spec.initial_name == "OFF"
    assert spec.initial_id == 0


def test_ast_to_spec_transitions():
    spec = ast_to_spec(_TREE)
    assert len(spec.transitions) == 2
    t0 = spec.transitions[0]
    assert t0.from_name == "OFF"
    assert t0.to_name == "ON"
    assert t0.guard == "button_pressed"
    assert t0.action == "light_on"
    t1 = spec.transitions[1]
    assert t1.from_name == "ON"
    assert t1.to_name == "OFF"
    assert t1.guard == "button_pressed"
    assert t1.action == "light_off"


def test_ast_to_spec_guards_actions():
    spec = ast_to_spec(_TREE)
    assert spec.guards == ["button_pressed"]
    assert spec.actions == ["light_on", "light_off"]


def test_ast_to_spec_no_states_raises():
    with pytest.raises(ValueError, match="No \\*_STATE_\\*"):
        ast_to_spec(ast.parse("x = 1"))


def test_ast_to_spec_no_transitions_raises():
    src = "LIGHT_STATE_OFF = 0\nLIGHT_STATE_ON = 1\n"
    with pytest.raises(ValueError, match="No Transition"):
        ast_to_spec(ast.parse(src))


def test_ast_to_spec_no_machine_raises():
    src = """\
LIGHT_STATE_OFF = 0
LIGHT_STATE_ON = 1
_T = [Transition(LIGHT_STATE_OFF, LIGHT_STATE_ON)]
"""
    with pytest.raises(ValueError, match="No Machine"):
        ast_to_spec(ast.parse(src))


def test_transition_without_guard_action():
    src = """\
X_STATE_A = 0
X_STATE_B = 1
_T = [Transition(X_STATE_A, X_STATE_B)]
def f():
    return Machine(name="x", state=X_STATE_A, transitions=_T)
"""
    spec = ast_to_spec(ast.parse(src))
    assert spec.transitions[0].guard is None
    assert spec.transitions[0].action is None


# ── CLI integration ───────────────────────────────────────────────────────────

RXNET_PY_ROOT = Path(__file__).resolve().parents[2] / "python"
LIGHT_FSM_PY = RXNET_PY_ROOT / "examples" / "fsm" / "01-light" / "light_fsm.py"


@pytest.mark.skipif(not LIGHT_FSM_PY.exists(), reason="light_fsm.py not found")
def test_integration_light_fsm_py():
    from rxnet.tools.extract_py import parse_python
    spec = parse_python(LIGHT_FSM_PY)
    assert spec.name == "light"
    assert {s.name for s in spec.states} == {"OFF", "ON"}
    assert spec.initial_name == "OFF"
    assert len(spec.transitions) == 2
    assert {t.guard for t in spec.transitions} == {"button_pressed"}


@pytest.mark.skipif(not LIGHT_FSM_PY.exists(), reason="light_fsm.py not found")
def test_integration_cli_py(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    rc = m.main([str(LIGHT_FSM_PY), "--dot"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "digraph" in out
    assert "OFF" in out
    assert "button_pressed" in out
