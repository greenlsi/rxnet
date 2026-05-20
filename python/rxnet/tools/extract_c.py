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
                result[name] = int(child["value"])
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


# ── Additional public functions (stubs for now) ────────────────────────────────

def ast_to_spec(ast: dict) -> FsmSpec:
    """Convert AST to FsmSpec (implementation pending)."""
    raise NotImplementedError()


def generate_dot(spec: FsmSpec) -> str:
    """Generate Graphviz DOT format (implementation pending)."""
    raise NotImplementedError()


def generate_mermaid(spec: FsmSpec) -> str:
    """Generate Mermaid diagram format (implementation pending)."""
    raise NotImplementedError()
