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

def _collect_all_enum_constants(ast: dict) -> dict[str, int]:
    """Return {ENUM_NAME: int_value} for every EnumConstantDecl in the AST."""
    result: dict[str, int] = {}
    for node in _find_all(ast, "EnumConstantDecl"):
        name = node.get("name", "")
        if not name:
            continue
        for child in node.get("inner", []):
            if child.get("kind") in ("ConstantExpr", "IntegerLiteral"):
                value = child.get("value")
                if value is not None:
                    result[name] = int(value)
                break
    return result


def _transition_state_names(
    raw_transitions: list[tuple[str | None, str | None, str | None, str | None]],
) -> set[str]:
    """Collect all state constant names actually referenced in the transition table."""
    names: set[str] = set()
    for from_c, to_c, _, _ in raw_transitions:
        if from_c:
            names.add(from_c)
        if to_c:
            names.add(to_c)
    return names


def _common_underscore_prefix(names: list[str]) -> str:
    """Longest common underscore-delimited prefix shared by all names.

    Examples:
      ["AUTO_STATE_OFF", "AUTO_STATE_ON"]          → "AUTO_STATE_"
      ["RAFT_ROLE_FOLLOWER", "RAFT_ROLE_LEADER"]   → "RAFT_ROLE_"
      ["S_OFF", "S_ON"]                            → "S_"
    """
    if not names:
        return ""
    parts_lists = [n.split("_") for n in names]
    common: list[str] = []
    for group in zip(*parts_lists):
        if len(set(group)) == 1:
            common.append(group[0])
        else:
            break
    return "_".join(common) + "_" if common else ""


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


def _find_machine_init(ast: dict) -> tuple[str | None, str | None]:
    """Return (machine_name_or_None, initial_state_const_or_None).

    machine_name is None when the name argument is a runtime variable rather than
    a string literal (e.g. ``config->node_id``).  Callers should fall back to the
    source file's stem in that case.
    """
    for node in _find_all(ast, "CallExpr"):
        inner = node.get("inner", [])
        if len(inner) < 4:
            continue
        if _first_declref(inner[0]) != "rx_fsm_machine_init":
            continue
        machine_name = _first_string_literal(inner[2])   # None if not a literal
        initial_const = _first_declref(inner[3])
        if initial_const:
            return machine_name, initial_const
    return None, None


# ── output generators ──────────────────────────────────────────────────────────

def ast_to_spec(ast: dict, name_hint: str = "fsm") -> FsmSpec:
    """Extract an FsmSpec from a clang AST JSON dict.

    State constants are discovered from the names actually used in the
    ``rx_fsm_transition[]`` array — no naming convention is assumed.
    The common underscore-delimited prefix (e.g. ``RAFT_ROLE_`` or ``AUTO_STATE_``)
    is stripped automatically to produce clean state names.

    ``name_hint`` is used as the module/machine name when the ``rx_fsm_machine_init``
    call passes a runtime variable rather than a string literal.
    """
    raw_transitions = _find_transitions(ast)
    machine_name, initial_const = _find_machine_init(ast)

    if initial_const is None:
        raise ValueError("No rx_fsm_machine_init call found")

    # Discover state constants from what the transition table actually references
    state_const_names = _transition_state_names(raw_transitions)
    if not state_const_names and initial_const:
        state_const_names.add(initial_const)
    all_consts = _collect_all_enum_constants(ast)
    enum_constants = {k: v for k, v in all_consts.items() if k in state_const_names}

    if not enum_constants:
        raise ValueError(
            f"Could not resolve state enum constants "
            f"{sorted(state_const_names)} in the AST"
        )

    # Determine the common prefix to strip (works with any naming convention)
    prefix = _common_underscore_prefix(sorted(enum_constants))

    def _state_suffix(const_name: str) -> str:
        return const_name[len(prefix):] if const_name.startswith(prefix) else const_name

    states = [
        FsmState(id=v, name=_state_suffix(k))
        for k, v in sorted(enum_constants.items(), key=lambda x: x[1])
    ]
    by_name = {s.name: s.id for s in states}

    initial_name = _state_suffix(initial_const)
    resolved_machine_name = machine_name if machine_name else name_hint

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
        name=resolved_machine_name,
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


# ── clang runner ───────────────────────────────────────────────────────────────

def run_clang_ast(c_file: Path, extra_args: list[str]) -> dict:
    """Run clang -ast-dump=json and return the parsed JSON dict."""
    cmd = [
        "clang",
        "-Xclang", "-ast-dump=json",
        "-fsyntax-only",
        str(c_file),
        *extra_args,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "clang exited non-zero")
    return json.loads(result.stdout)


# ── CLI ────────────────────────────────────────────────────────────────────────

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="rxnet-extract",
        description=(
            "Extract FSM topology from a rxnet source file (.c or .py) and emit "
            "one or more output formats."
        ),
        epilog="""
output formats (at least one required):
  --tla      TLA+ module suitable for TLC model checking
  --dot      Graphviz DOT digraph (pipe to 'dot -Tsvg' or 'dot -Tpdf')
  --mermaid  Mermaid stateDiagram-v2 (renders in GitHub Markdown)

C source (extra arguments forwarded to clang, e.g. -I, -D):
  rxnet-extract light_fsm.c -Iinclude --dot
  rxnet-extract light_fsm.c -Iinclude --tla --dot
  rxnet-extract light_fsm.c -Iinclude --dot | dot -Tsvg -o light.svg

Python source (no extra arguments needed):
  rxnet-extract light_fsm.py --dot
  rxnet-extract light_fsm.py --tla --mermaid

multiple formats are separated by '---' on stdout:
  rxnet-extract light_fsm.c -Iinclude --tla --dot --mermaid > all.txt
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("file", metavar="FILE",
                    help="rxnet FSM source file (.c or .py)")
    ap.add_argument("--tla",     action="store_true", help="emit TLA+ spec")
    ap.add_argument("--dot",     action="store_true", help="emit Graphviz DOT")
    ap.add_argument("--mermaid", action="store_true", help="emit Mermaid stateDiagram-v2")

    args, clang_args = ap.parse_known_args(argv)

    if not (args.tla or args.dot or args.mermaid):
        ap.error("at least one of --tla, --dot, --mermaid is required")

    src_file = Path(args.file).resolve()
    suffix = src_file.suffix.lower()

    try:
        if suffix == ".py":
            from rxnet.tools.extract_py import parse_python
            spec = parse_python(src_file)
        elif suffix == ".c":
            ast_json = run_clang_ast(src_file, clang_args)
            spec = ast_to_spec(ast_json, name_hint=src_file.stem)
        else:
            print(f"error: unsupported file type {suffix!r} (expected .c or .py)",
                  file=sys.stderr)
            return 1
    except (RuntimeError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    outputs = []
    if args.tla:
        outputs.append(generate_tla(spec))
    if args.dot:
        outputs.append(generate_dot(spec))
    if args.mermaid:
        outputs.append(generate_mermaid(spec))

    print("\n---\n".join(outputs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
