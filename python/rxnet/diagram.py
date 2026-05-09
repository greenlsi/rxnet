# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet.diagram — Graphviz DOT generation for FSM and Petri Net models.

Functions are pure: they read existing topology from Machine / Net objects
and return DOT source strings.  No side effects, no file I/O.

Usage::

    from rxnet.diagram import fsm_to_dot, pn_to_dot

    dot = fsm_to_dot(machine)
    dot = pn_to_dot(net)
"""
from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .fsm import Machine
    from .pn import Net


def fsm_to_dot(machine: Machine, initial_state: int | None = None) -> str:
    """Return a Graphviz DOT string for *machine*.

    Parameters
    ----------
    machine:
        The FSM machine.  Reads ``machine.transitions``, ``machine.state_names``,
        and ``machine.name``.
    initial_state:
        The state to mark as initial.  Defaults to ``machine.state`` (correct
        when called before the first tick, or snapshotted at ``Tracer.attach()``).
    """
    names = machine.state_names or {}
    init  = machine.state if initial_state is None else initial_state

    def sname(s: int) -> str:
        return _q(names.get(s, f"s{s}"))

    lines = [
        f"// FSM: {machine.name}",
        "digraph {",
        "    rankdir=LR;",
        '    node [shape=circle fontname="Helvetica" fontsize=11];',
        '    __start [shape=point width=0.2];',
        f"    __start -> {sname(init)};",
    ]
    for t in machine.transitions:
        lbl = _arc_label(t.label, t.guard)
        lines.append(f"    {sname(t.from_state)} -> {sname(t.to_state)} "
                     f'[label={_q(lbl)} fontsize=10];')
    lines.append("}")
    return "\n".join(lines)


def pn_to_dot(net: Net) -> str:
    """Return a Graphviz DOT string for *net*.

    Reads ``net.transitions``, ``net.place_names``, ``net.transition_names``,
    ``net.places`` (for initial token counts), and ``net.name``.
    """
    pnames = net.place_names or {}
    tnames = net.transition_names or []

    def pname(i: int) -> str:
        label = pnames.get(i, f"P{i}")
        tokens = net.places[i]
        mark = "●" * tokens if 0 < tokens <= 4 else (f"{tokens}●" if tokens else "")
        return _q(f"{label}\\n{mark}" if mark else label)

    def tname(i: int) -> str:
        return _q(tnames[i] if i < len(tnames) else f"T{i}")

    lines = [
        f"// Petri Net: {net.name}",
        "digraph {",
        "    rankdir=LR;",
    ]
    # places — circles
    lines.append('    node [shape=circle fontname="Helvetica" fontsize=11];')
    for i in range(len(net.places)):
        lines.append(f"    {pname(i)};")
    # transitions — rectangles
    lines.append('    node [shape=rectangle fontname="Helvetica" fontsize=11 '
                 'width=0.3 height=0.6 style=filled fillcolor="#444444" fontcolor=white];')
    for i in range(len(net.transitions)):
        lines.append(f"    {tname(i)};")
    # arcs
    for i, trans in enumerate(net.transitions):
        f' [label={_q(trans.label)} fontsize=10]' if trans.label else ""
        for arc in trans.consume:
            w = f' [label="{arc.weight}" fontsize=10]' if arc.weight != 1 else ""
            lines.append(f"    {pname(arc.place_id)} -> {tname(i)}{w};")
        for arc in trans.produce:
            w = f' [label="{arc.weight}" fontsize=10]' if arc.weight != 1 else ""
            lines.append(f"    {tname(i)} -> {pname(arc.place_id)}{w};")
    lines.append("}")
    return "\n".join(lines)


# ── helpers ────────────────────────────────────────────────────────────────

def _q(s: str) -> str:
    """Wrap a string in DOT double-quotes, escaping internal quotes."""
    return '"' + s.replace('"', '\\"') + '"'


def _arc_label(label: str | None, guard: object | None) -> str:
    if label:
        return label
    if guard is not None and hasattr(guard, "__name__"):
        return guard.__name__
    return ""
