# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet FSM extractor for Python source files via stdlib ast."""
from __future__ import annotations

import ast
from pathlib import Path

from rxnet.tools.gen import FsmSpec, FsmState, FsmTransition

# ── AST helpers ───────────────────────────────────────────────────────────────

def _collect_enum_constants(tree: ast.Module) -> dict[str, int]:
    """Return {NAME: int_value} for all *_STATE_* integer assignments."""
    result: dict[str, int] = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign):
            continue
        if len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            continue
        name = node.targets[0].id
        if "_STATE_" not in name:
            continue
        if isinstance(node.value, ast.Constant) and isinstance(node.value.value, int):
            result[name] = node.value.value
    return result


def _find_transitions(
    tree: ast.Module,
) -> list[tuple[str | None, str | None, str | None, str | None]]:
    """Return [(from_const, to_const, guard, action), ...] from a Transition list."""
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.List):
            continue
        transitions = []
        for elt in node.value.elts:
            if not isinstance(elt, ast.Call):
                break
            if not isinstance(elt.func, ast.Name) or elt.func.id != "Transition":
                break
            args = elt.args
            from_c = args[0].id if args and isinstance(args[0], ast.Name) else None
            to_c = args[1].id if len(args) > 1 and isinstance(args[1], ast.Name) else None
            guard = None
            action = None
            for kw in elt.keywords:
                if kw.arg == "guard" and isinstance(kw.value, ast.Name):
                    guard = kw.value.id.lstrip("_")
                elif kw.arg == "action" and isinstance(kw.value, ast.Name):
                    action = kw.value.id.lstrip("_")
            transitions.append((from_c, to_c, guard, action))
        if transitions:
            return transitions
    return []


def _find_machine_init(tree: ast.Module) -> tuple[str, str] | None:
    """Return (machine_name, initial_state_const) from a Machine(...) call."""
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if not isinstance(node.func, ast.Name) or node.func.id != "Machine":
            continue
        name = None
        state_const = None
        for kw in node.keywords:
            if (kw.arg == "name"
                    and isinstance(kw.value, ast.Constant)
                    and isinstance(kw.value.value, str)):
                name = kw.value.value
            elif kw.arg == "state" and isinstance(kw.value, ast.Name):
                state_const = kw.value.id
        if name and state_const:
            return name, state_const
    return None


# ── spec builder ──────────────────────────────────────────────────────────────

def ast_to_spec(tree: ast.Module) -> FsmSpec:
    """Extract an FsmSpec from a parsed Python module AST."""
    enum_constants = _collect_enum_constants(tree)
    raw_transitions = _find_transitions(tree)
    init_info = _find_machine_init(tree)

    if not enum_constants:
        raise ValueError("No *_STATE_* integer constants found")
    if not raw_transitions:
        raise ValueError("No Transition list found")
    if init_info is None:
        raise ValueError("No Machine(...) call found")

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


def parse_python(path: Path) -> FsmSpec:
    """Parse a Python FSM source file and return an FsmSpec."""
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(path))
    return ast_to_spec(tree)
