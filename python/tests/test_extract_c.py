# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.tools.extract_c import (
    _collect_all_enum_constants,
    _find_machine_init,
    _find_transitions,
    ast_to_spec,
    generate_dot,
    generate_mermaid,
)


def _declref(name: str) -> dict:
    return {"kind": "DeclRefExpr", "referencedDecl": {"name": name}}


def _cast(name: str) -> dict:
    return {"kind": "ImplicitCastExpr", "inner": [_declref(name)]}


def _cast_str(value: str) -> dict:
    return {"kind": "ImplicitCastExpr", "inner": [{"kind": "StringLiteral", "value": value}]}


def _transition(from_: str, to: str, guard: str, action: str) -> dict:
    return {"kind": "InitListExpr", "inner": [
        _declref(from_), _declref(to), _cast(guard), _cast(action),
    ]}


LIGHT_AST: dict = {
    "kind": "TranslationUnitDecl",
    "inner": [
        {
            "kind": "EnumDecl",
            "inner": [
                {
                    "kind": "EnumConstantDecl",
                    "name": "LIGHT_STATE_OFF",
                    "inner": [{"kind": "ConstantExpr", "value": 0}],
                },
                {
                    "kind": "EnumConstantDecl",
                    "name": "LIGHT_STATE_ON",
                    "inner": [{"kind": "ConstantExpr", "value": 1}],
                },
            ],
        },
        {
            "kind": "VarDecl",
            "name": "transitions",
            "type": {"qualType": "const rx_fsm_transition[2]"},
            "inner": [
                {
                    "kind": "InitListExpr",
                    "inner": [
                        _transition("LIGHT_STATE_OFF", "LIGHT_STATE_ON",
                                    "button_pressed", "light_on"),
                        _transition("LIGHT_STATE_ON", "LIGHT_STATE_OFF",
                                    "button_pressed", "light_off"),
                    ],
                }
            ],
        },
        {
            "kind": "CallExpr",
            "inner": [
                _cast("rx_fsm_machine_init"),
                _cast("machine"),
                _cast_str('"light"'),
                _declref("LIGHT_STATE_OFF"),
                _cast("transitions"),
            ],
        },
    ],
}


def test_collect_enum_constants():
    result = _collect_all_enum_constants(LIGHT_AST)
    assert result == {"LIGHT_STATE_OFF": 0, "LIGHT_STATE_ON": 1}


def test_find_transitions():
    pairs = _find_transitions(LIGHT_AST)
    assert pairs == [
        ("LIGHT_STATE_OFF", "LIGHT_STATE_ON", "button_pressed", "light_on"),
        ("LIGHT_STATE_ON", "LIGHT_STATE_OFF", "button_pressed", "light_off"),
    ]


def test_find_machine_init():
    result = _find_machine_init(LIGHT_AST)
    assert result == ("light", "LIGHT_STATE_OFF")


def test_ast_to_spec_states():
    spec = ast_to_spec(LIGHT_AST)
    assert spec.name == "light"
    assert len(spec.states) == 2
    assert spec.states[0].name == "OFF"
    assert spec.states[0].id == 0
    assert spec.states[1].name == "ON"
    assert spec.states[1].id == 1


def test_ast_to_spec_initial():
    spec = ast_to_spec(LIGHT_AST)
    assert spec.initial_name == "OFF"
    assert spec.initial_id == 0


def test_ast_to_spec_transitions():
    spec = ast_to_spec(LIGHT_AST)
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
    spec = ast_to_spec(LIGHT_AST)
    assert spec.guards == ["button_pressed"]
    assert spec.actions == ["light_on", "light_off"]


def test_generate_dot_contains_states():
    spec = ast_to_spec(LIGHT_AST)
    dot = generate_dot(spec)
    assert "OFF" in dot
    assert "ON" in dot
    assert "__start" in dot
    assert "button_pressed" in dot


def test_generate_dot_initial_arrow():
    spec = ast_to_spec(LIGHT_AST)
    dot = generate_dot(spec)
    assert '__start -> "OFF"' in dot


def test_generate_dot_transition_labels():
    spec = ast_to_spec(LIGHT_AST)
    dot = generate_dot(spec)
    assert "button_pressed / light_on" in dot
    assert "button_pressed / light_off" in dot


def test_generate_mermaid_format():
    spec = ast_to_spec(LIGHT_AST)
    mmd = generate_mermaid(spec)
    assert mmd.startswith("stateDiagram-v2")
    assert "[*] --> OFF" in mmd
    assert "OFF --> ON" in mmd
    assert "ON --> OFF" in mmd
    assert "button_pressed / light_on" in mmd


def test_generate_tla_reuse():
    from rxnet.tools.gen import generate_tla
    spec = ast_to_spec(LIGHT_AST)
    tla = generate_tla(spec)
    assert "MODULE light_fsm" in tla
    assert "S_OFF == 0" in tla
    assert "S_ON == 1" in tla
    assert "T_OFF_ON_button_pressed" in tla


# ── CLI tests ─────────────────────────────────────────────────────────────────

def test_main_dot(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    monkeypatch.setattr(m, "run_clang_ast", lambda path, args: LIGHT_AST)
    rc = m.main(["dummy.c", "--dot"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "digraph" in out
    assert "OFF" in out


def test_main_tla(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    monkeypatch.setattr(m, "run_clang_ast", lambda path, args: LIGHT_AST)
    rc = m.main(["dummy.c", "--tla"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "MODULE light_fsm" in out


def test_main_mermaid(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    monkeypatch.setattr(m, "run_clang_ast", lambda path, args: LIGHT_AST)
    rc = m.main(["dummy.c", "--mermaid"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "stateDiagram-v2" in out


def test_main_multiple_formats(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    monkeypatch.setattr(m, "run_clang_ast", lambda path, args: LIGHT_AST)
    rc = m.main(["dummy.c", "--dot", "--tla"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "digraph" in out
    assert "MODULE light_fsm" in out
    assert "---" in out


def test_main_no_format_exits_nonzero(monkeypatch):
    import rxnet.tools.extract_c as m
    monkeypatch.setattr(m, "run_clang_ast", lambda path, args: LIGHT_AST)
    with pytest.raises(SystemExit) as exc_info:
        m.main(["dummy.c"])
    assert exc_info.value.code != 0


def test_main_clang_error_returns_1(monkeypatch, capsys):
    import rxnet.tools.extract_c as m
    def boom(path, args):
        raise RuntimeError("clang not found")
    monkeypatch.setattr(m, "run_clang_ast", boom)
    rc = m.main(["dummy.c", "--dot"])
    assert rc == 1


# ── integration tests — requires clang on PATH ────────────────────────────────

RXNET_C_ROOT = Path(__file__).resolve().parents[2] / "c"
LIGHT_FSM_C  = RXNET_C_ROOT / "examples" / "fsm" / "01-light" / "light_fsm.c"
_CLANG_PATH = Path("/usr/bin/clang")
_CLANG_EXISTS = _CLANG_PATH.exists()


@pytest.mark.skipif(
    not _CLANG_EXISTS or not LIGHT_FSM_C.exists(),
    reason="clang not found or light_fsm.c not found",
)
def test_integration_light_fsm():
    from rxnet.tools.extract_c import run_clang_ast
    ast = run_clang_ast(LIGHT_FSM_C, [f"-I{RXNET_C_ROOT / 'include'}"])
    spec = ast_to_spec(ast)
    assert spec.name == "light"
    assert {s.name for s in spec.states} == {"OFF", "ON"}
    assert spec.initial_name == "OFF"
    assert len(spec.transitions) == 2
    guards = {t.guard for t in spec.transitions}
    assert guards == {"button_pressed"}


@pytest.mark.skipif(
    not _CLANG_EXISTS or not LIGHT_FSM_C.exists(),
    reason="clang not found or light_fsm.c not found",
)
def test_integration_dot_output():
    from rxnet.tools.extract_c import run_clang_ast
    ast = run_clang_ast(LIGHT_FSM_C, [f"-I{RXNET_C_ROOT / 'include'}"])
    spec = ast_to_spec(ast)
    dot = generate_dot(spec)
    assert "digraph" in dot
    assert '"OFF"' in dot
    assert '"ON"' in dot
    assert "button_pressed" in dot
