# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet-extract-c — FSM topology extractor from C source via clang AST dump.

Usage::

    python -m rxnet.tools.extract_c light_fsm.c -Iinclude --tla
    python -m rxnet.tools.extract_c light_fsm.c -Iinclude --dot | dot -Tsvg -o light.svg
    python -m rxnet.tools.extract_c light_fsm.c -Iinclude --mermaid
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from rxnet.tools.gen import FsmSpec, FsmState, FsmTransition, generate_tla

# ── AST traversal helpers ──────────────────────────────────────────────────────

def _find_all(node: dict, kind: str, results: list[dict] | None = None) -> list[dict]:
    if results is None:
        results = []
    if node.get("kind") == kind:
        results.append(node)
    for child in node.get("inner", []):
        _find_all(child, kind, results)
    return results


def _first_declref(node: dict) -> str | None:
    if node.get("kind") == "DeclRefExpr":
        return node.get("referencedDecl", {}).get("name")
    for child in node.get("inner", []):
        result = _first_declref(child)
        if result is not None:
            return result
    return None


def _first_string_literal(node: dict) -> str | None:
    if node.get("kind") == "StringLiteral":
        return node.get("value", "").strip('"')
    for child in node.get("inner", []):
        result = _first_string_literal(child)
        if result is not None:
            return result
    return None


# ── three targeted extraction functions ───────────────────────────────────────

def _collect_enum_constants(ast: dict) -> dict[str, int]:
    """Return {ENUM_NAME: int_value} for all *_STATE_* EnumConstantDecl nodes."""
    result: dict[str, int] = {}
    for node in _find_all(ast, "EnumConstantDecl"):
        name = node.get("name", "")
        if "_STATE_" not in name:
            continue
        for child in node.get("inner", []):
            if child.get("kind") in ("ConstantExpr", "IntegerLiteral"):
                value = child.get("value")
                if value is not None:
                    result[name] = int(value)
                break
    return result


def _find_transitions(
    ast: dict,
) -> list[tuple[str | None, str | None, str | None, str | None]]:
    """Return [(from_const, to_const, guard, action), ...] from the transitions VarDecl."""
    for node in _find_all(ast, "VarDecl"):
        qual_type = node.get("type", {}).get("qualType", "")
        if "rx_fsm_transition[" not in qual_type:
            continue
        outer = node.get("inner", [])
        if not outer or outer[0].get("kind") != "InitListExpr":
            continue
        transitions = []
        for trans in outer[0].get("inner", []):
            if trans.get("kind") != "InitListExpr":
                continue
            fields = trans.get("inner", [])
            from_c = _first_declref(fields[0]) if len(fields) > 0 else None
            to_c = _first_declref(fields[1]) if len(fields) > 1 else None
            guard = _first_declref(fields[2]) if len(fields) > 2 else None
            action = _first_declref(fields[3]) if len(fields) > 3 else None
            transitions.append((from_c, to_c, guard, action))
        return transitions
    return []


def _find_machine_init(ast: dict) -> tuple[str, str] | None:
    """Return (machine_name, initial_state_const) from the rx_fsm_machine_init call."""
    for node in _find_all(ast, "CallExpr"):
        inner = node.get("inner", [])
        if len(inner) < 4:
            continue
        if _first_declref(inner[0]) != "rx_fsm_machine_init":
            continue
        machine_name = _first_string_literal(inner[2])
        initial_const = _first_declref(inner[3])
        if machine_name and initial_const:
            return machine_name, initial_const
    return None


# ── output generators ──────────────────────────────────────────────────────────

def ast_to_spec(ast: dict) -> FsmSpec:
    """Extract an FsmSpec from a clang AST JSON dict."""
    enum_constants = _collect_enum_constants(ast)
    raw_transitions = _find_transitions(ast)
    init_info = _find_machine_init(ast)

    if not enum_constants:
        raise ValueError("No *_STATE_* enum constants found")
    if init_info is None:
        raise ValueError("No rx_fsm_machine_init call found")

    machine_name, initial_const = init_info

    def _state_suffix(const_name: str) -> str:
        return const_name.split("_STATE_", 1)[1] if "_STATE_" in const_name else const_name

    states = [
        FsmState(id=v, name=_state_suffix(k))
        for k, v in sorted(enum_constants.items(), key=lambda x: x[1])
    ]
    by_name = {s.name: s.id for s in states}

    initial_name = _state_suffix(initial_const)

    transitions: list[FsmTransition] = []
    for from_c, to_c, guard, action in raw_transitions:
        from_name = _state_suffix(from_c) if from_c else ""
        to_name = _state_suffix(to_c) if to_c else ""
        transitions.append(FsmTransition(
            from_id=by_name[from_name],
            to_id=by_name[to_name],
            from_name=from_name,
            to_name=to_name,
            guard=guard,
            action=action,
        ))

    guards = list(dict.fromkeys(t.guard for t in transitions if t.guard))
    actions = list(dict.fromkeys(t.action for t in transitions if t.action))

    return FsmSpec(
        name=machine_name,
        params=[],
        states=states,
        initial_id=by_name[initial_name],
        initial_name=initial_name,
        transitions=transitions,
        guards=guards,
        actions=actions,
    )


def generate_dot(spec: FsmSpec) -> str:
    """Graphviz DOT digraph from FsmSpec."""
    lines = [
        f"// FSM: {spec.name}",
        "digraph {",
        "    rankdir=LR;",
        '    node [shape=circle fontname="Helvetica" fontsize=11];',
        '    __start [shape=point width=0.2];',
        f'    __start -> "{spec.initial_name}";',
    ]
    for t in spec.transitions:
        label = t.guard or ""
        if t.action:
            label = f"{label} / {t.action}" if label else t.action
        if not label:
            label = "—"
        lines.append(
            f'    "{t.from_name}" -> "{t.to_name}" [label="{label}" fontsize=10];'
        )
    lines.append("}")
    return "\n".join(lines)


def generate_mermaid(spec: FsmSpec) -> str:
    """Mermaid stateDiagram-v2 from FsmSpec."""
    lines = ["stateDiagram-v2", f"    [*] --> {spec.initial_name}"]
    for t in spec.transitions:
        label = t.guard or ""
        if t.action:
            label = f"{label} / {t.action}" if label else t.action
        arrow = f"    {t.from_name} --> {t.to_name}"
        if label:
            arrow += f" : {label}"
        lines.append(arrow)
    return "\n".join(lines)
