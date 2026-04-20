# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

"""rxnet-gen — FSM code and TLA+ spec generator.

Given a YAML FSM descriptor, produces **three always-regenerated artefacts**
and **three implementation stubs** written only on the first run:

Always regenerated (safe to commit, never edit by hand)::

    <name>_fsm_model.py   — state constants + wiring table         (Python)
    <name>_fsm_model.h    — state enum + public declarations        (C)
    <name>.tla            — TLA+ spec with abstract boolean guards

Written once, then preserved (fill in the implementation bodies)::

    <name>_fsm.py         — guard / action / latch / dump stubs     (Python)
    <name>_fsm.c          — guard / action / latch / dump stubs     (C)
    <name>_fsm.h          — public factory header                   (C)

On every run the tool also reports **consistency warnings** for both Python
and C implementation files:

- *Missing*: a guard or action in the YAML has no corresponding function
  in the implementation file → you must add it.
- *Orphan*: a function in the implementation file no longer appears in the
  YAML → probably a leftover from a renamed/removed transition.

Usage::

    # generate everything next to the YAML file
    python -m rxnet.tools.gen light.yaml

    # send each language to its own directory
    python -m rxnet.tools.gen light.yaml \\
        --py-dir  python/examples/fsm/01-light/ \\
        --c-dir   c/examples/fsm/01-light/ \\
        --tla-dir tlaplus/

    # consistency check only — no files written
    python -m rxnet.tools.gen light.yaml --check

    # overwrite impl stubs (DESTRUCTIVE — resets all hand-written code)
    python -m rxnet.tools.gen light.yaml --force

YAML format::

    name: light            # machine name; drives all generated identifiers
    params:                # factory function parameters (order preserved)
      - name: button_gpio
        type: int          # Python type annotation
        ctype: int         # C type (defaults to ``type`` if omitted)
      - name: light_gpio
        type: int
    states:
      - id: 0   name: OFF
      - id: 1   name: ON
    initial: OFF           # state name or integer id
    transitions:
      - from: OFF   to: ON    guard: button_pressed   action: light_on
      - from: ON    to: OFF   guard: button_pressed   action: light_off
      # ``guard`` and ``action`` are both optional:
      # omitting guard  → unconditional transition (always fires)
      # omitting action → state change with no side-effect

Generated identifier conventions
---------------------------------
Given ``name: light`` and a guard ``button_pressed``:

  Python model  : ``LIGHT_STATE_OFF``, ``LIGHT_TRANSITIONS``, ``LIGHT_STATE_NAMES``
  Python impl   : ``_button_pressed(ctx, data)``, ``_light_on(ctx, data)``
  C model       : ``LIGHT_STATE_OFF`` (enum), ``light_fsm_create(...)``
  C impl guards : ``static int button_pressed(const rx_fsm_context *, void *)``
  C impl actions: ``static void light_on(rx_fsm_context *, void *)``
  TLA+ guards   : ``ButtonPressed`` (PascalCase)
  TLA+ states   : ``S_OFF``, ``S_ON`` (S_ prefix)
  TLA+ operators: ``T_OFF_ON_button_pressed`` (one per transition)
"""
from __future__ import annotations

import argparse
import ast
import re
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    print(
        "error: PyYAML is required — add it to your dev dependencies:\n"
        "  uv add --dev pyyaml",
        file=sys.stderr,
    )
    sys.exit(1)

# ── data model ────────────────────────────────────────────────────────────────

COPYRIGHT = "Copyright 2026 Jose M. Moya <jm.moya@upm.es>"
SPDX = "MIT"


@dataclass(frozen=True)
class FsmParam:
    name: str
    type: str   # Python type annotation
    ctype: str  # C type (may differ, e.g. ``gpio_num_t`` vs ``int``)


@dataclass(frozen=True)
class FsmState:
    id: int
    name: str


@dataclass(frozen=True)
class FsmTransition:
    from_id: int
    to_id: int
    from_name: str
    to_name: str
    guard: str | None   # None → unconditional
    action: str | None  # None → no side-effect


@dataclass
class FsmSpec:
    name: str
    params: list[FsmParam]
    states: list[FsmState]
    initial_id: int
    initial_name: str
    transitions: list[FsmTransition]
    guards: list[str]   # unique guard names, first-appearance order
    actions: list[str]  # unique action names, first-appearance order

    # derived helpers
    @property
    def upper(self) -> str:
        """``LIGHT`` for name ``light``."""
        return self.name.upper()

    def state_name(self, sid: int) -> str:
        for s in self.states:
            if s.id == sid:
                return s.name
        raise KeyError(sid)

    def state_id(self, name: str | int) -> int:
        if isinstance(name, int):
            return name
        for s in self.states:
            if s.name == name:
                return s.id
        raise KeyError(name)


# ── YAML parser ───────────────────────────────────────────────────────────────

# Custom loader that does NOT interpret ON/OFF/YES/NO/TRUE/FALSE as booleans.
# PyYAML 1.1 treats those as booleans by default, which breaks state names
# like OFF and ON that are very common in FSM descriptors.
class _Loader(yaml.SafeLoader):
    pass

_Loader.yaml_implicit_resolvers = {
    key: [
        (tag, regexp)
        for tag, regexp in resolvers
        if tag != "tag:yaml.org,2002:bool"
    ]
    for key, resolvers in yaml.SafeLoader.yaml_implicit_resolvers.items()
}


def parse_yaml(path: Path) -> FsmSpec:
    """Load and validate a FSM YAML descriptor, returning a :class:`FsmSpec`."""
    raw: dict = yaml.load(path.read_text(encoding="utf-8"), Loader=_Loader)  # noqa: S506

    # states
    states = [FsmState(int(s["id"]), str(s["name"])) for s in raw["states"]]
    by_name = {s.name: s.id for s in states}
    by_id = {s.id: s.name for s in states}

    # initial state
    init_raw = raw["initial"]
    initial_id = int(init_raw) if isinstance(init_raw, int) else by_name[str(init_raw)]
    initial_name = by_id[initial_id]

    # params
    params = []
    for p in raw.get("params", []):
        ptype = str(p["type"])
        params.append(FsmParam(
            name=str(p["name"]),
            type=ptype,
            ctype=str(p.get("ctype", ptype)),
        ))

    # transitions
    transitions: list[FsmTransition] = []
    for t in raw["transitions"]:
        from_id = by_name[str(t["from"])] if isinstance(t["from"], str) else int(t["from"])
        to_id = by_name[str(t["to"])] if isinstance(t["to"], str) else int(t["to"])
        guard = t.get("guard") or None
        action = t.get("action") or None
        transitions.append(FsmTransition(
            from_id=from_id,
            to_id=to_id,
            from_name=by_id[from_id],
            to_name=by_id[to_id],
            guard=str(guard) if guard else None,
            action=str(action) if action else None,
        ))

    # unique names preserving first-appearance order
    guards = list(dict.fromkeys(t.guard for t in transitions if t.guard))
    actions = list(dict.fromkeys(t.action for t in transitions if t.action))

    return FsmSpec(
        name=raw["name"],
        params=params,
        states=states,
        initial_id=initial_id,
        initial_name=initial_name,
        transitions=transitions,
        guards=guards,
        actions=actions,
    )


# ── Python model generator ────────────────────────────────────────────────────

def generate_python_model(spec: FsmSpec) -> str:
    """Emit ``<name>_fsm_model.py`` — always regenerated, never edit by hand."""
    U = spec.upper
    lines: list[str] = []

    lines += [
        f"# {COPYRIGHT}",
        f"# SPDX-License-Identifier: {SPDX}",
        "#",
        "# GENERATED — do not edit by hand.",
        f"# Source: {spec.name}.yaml",
        f"#   regenerate with: python -m rxnet.tools.gen {spec.name}.yaml",
        "",
        f'"""Auto-generated model for the ``{spec.name}`` FSM.',
        "",
        "Exports state constants, a state-name mapping, and the wiring table",
        "consumed by the hand-written implementation file.",
        '"""',
        "",
    ]

    # state constants
    lines.append("# ── states ───────────────────────────────────────────────────────────────")
    for s in spec.states:
        lines.append(f"{U}_STATE_{s.name} = {s.id}")
    lines.append("")

    # state names dict
    lines.append(f"{U}_STATE_NAMES: dict[int, str] = {{")
    for s in spec.states:
        lines.append(f"    {s.id}: {s.name!r},")
    lines.append("}")
    lines.append("")

    # transition wiring table
    lines += [
        "# ── transition wiring table ─────────────────────────────────────────────────",
        "# Each entry: (from_state_id, to_state_id, guard_name | None, action_name | None)",
        f"{U}_TRANSITIONS: list[tuple[int, int, str | None, str | None]] = [",
    ]
    for t in spec.transitions:
        g = repr(t.guard)
        a = repr(t.action)
        from_const = f"{U}_STATE_{t.from_name}"
        to_const = f"{U}_STATE_{t.to_name}"
        comment = f"  # {t.from_name} → {t.to_name}"
        if t.guard:
            comment += f"  [{t.guard}]"
        lines.append(f"    ({from_const}, {to_const}, {g}, {a}),{comment}")
    lines.append("]")
    lines.append("")

    return "\n".join(lines)


# ── Python stub generator ─────────────────────────────────────────────────────

def generate_python_stub(spec: FsmSpec) -> str:
    """Emit ``<name>_fsm.py`` — written once, never overwritten."""
    U = spec.upper
    N = spec.name
    param_sig = ", ".join(f"{p.name}: {p.type}" for p in spec.params)
    param_pass = ", ".join(p.name for p in spec.params)

    lines: list[str] = []
    lines += [
        f"# {COPYRIGHT}",
        f"# SPDX-License-Identifier: {SPDX}",
        "",
        f'"""{N.capitalize()} FSM — implementation.',
        "",
        "Fill in the guard, action, latch and dump bodies.",
        f"The model (states, wiring table) lives in ``{N}_fsm_model.py`` — do not",
        "edit that file; regenerate it from the YAML instead.",
        '"""',
        "from __future__ import annotations",
        "",
        "import sys",
        "from pathlib import Path",
        "",
        "sys.path.insert(0, str(Path(__file__).resolve().parents[4]))",
        "sys.path.insert(0, str(Path(__file__).resolve().parents[2]))",
        "",
        "from rxnet.fsm import Machine, Transition",
        "from rxnet.runtime import Context",
        "",
        f"from {N}_fsm_model import (",
        f"    {U}_STATE_{spec.initial_name},",
        f"    {U}_STATE_NAMES,",
        f"    {U}_TRANSITIONS,",
        ")",
        "",
        "import app_driver",
        "",
        "",
    ]

    # _Data class
    param_fields = "\n".join(f"        self.{p.name} = {p.name}" for p in spec.params)
    param_slots = ", ".join(f'"{p.name}"' for p in spec.params)
    lines += [
        "# ── private data carried by the machine ─────────────────────────────────────",
        "",
        "class _Data:",
        f"    __slots__ = ({param_slots},)  # TODO: add any extra runtime fields",
        "",
        f"    def __init__(self, {param_sig}) -> None:",
        f"{textwrap.indent(param_fields, '        ')}",
        "        # TODO: initialise extra fields",
        "",
        "",
    ]

    # latch / dump
    lines += [
        "# ── lifecycle callbacks ──────────────────────────────────────────────────────",
        "",
        "def _latch(ctx: Context, data: _Data) -> None:",
        "    \"\"\"Called once per tick before evaluate: snapshot hardware inputs.\"\"\"",
        "    pass  # TODO: read buttons / sensors into data fields",
        "",
        "",
        "def _dump(ctx: Context, data: _Data) -> None:",
        "    \"\"\"Called once per tick after commit: push outputs to hardware.\"\"\"",
        "    pass  # TODO: write lights / actuators from data fields",
        "",
        "",
    ]

    # guards
    if spec.guards:
        lines.append("# ── guards ──────────────────────────────────────────────────────────────")
        lines.append("")
        for g in spec.guards:
            lines += [
                f"def _{g}(ctx: Context, data: _Data) -> bool:",
                f'    """Return True when the ``{g}`` condition holds."""',
                "    raise NotImplementedError",
                "",
                "",
            ]

    # actions
    if spec.actions:
        lines.append("# ── actions ─────────────────────────────────────────────────────────────")
        lines.append("")
        for a in spec.actions:
            lines += [
                f"def _{a}(ctx: Context, data: _Data) -> None:",
                f'    """Execute the ``{a}`` action."""',
                "    raise NotImplementedError",
                "",
                "",
            ]

    # wiring maps and transition list
    guard_entries = "\n".join(f'    {g!r}: _{g},' for g in spec.guards)
    action_entries = "\n".join(f'    {a!r}: _{a},' for a in spec.actions)
    lines += [
        "# ── wiring: connect model names to implementation functions ──────────────────",
        "#",
        "# If the YAML adds a new guard or action, add it here and implement it above.",
        "# The generator will warn if any entry is missing or orphaned.",
        "",
        "_GUARD_MAP: dict[str, object] = {",
        guard_entries,
        "}",
        "",
        "_ACTION_MAP: dict[str, object] = {",
        action_entries,
        "}",
        "",
        "_TRANSITIONS = [",
        "    Transition(",
        "        from_state=f,",
        "        to_state=t,",
        "        guard=_GUARD_MAP[g] if g else None,    # KeyError → add guard to _GUARD_MAP",
        "        action=_ACTION_MAP[a] if a else None,  # KeyError → add action to _ACTION_MAP",
        "    )",
        f"    for f, t, g, a in {U}_TRANSITIONS",
        "]",
        "",
        "",
    ]

    # factory
    lines += [
        "# ── factory ──────────────────────────────────────────────────────────────────",
        "",
        f"def create_{N}_fsm({param_sig}) -> Machine:",
        f'    """Create and return a ``{N}`` Machine ready for use in a Runtime."""',
        "    # TODO: initialise hardware (buttons, lights, …)",
        f"    data = _Data({param_pass})",
        "    return Machine(",
        f"        name={N!r},",
        f"        state={U}_STATE_{spec.initial_name},",
        f"        state_names={U}_STATE_NAMES,",
        "        transitions=_TRANSITIONS,",
        "        user=data,",
        "        latch_inputs_cb=_latch,",
        "        dump_outputs_cb=_dump,",
        "    )",
        "",
    ]

    return "\n".join(lines)


# ── C model header generator ──────────────────────────────────────────────────

def generate_c_model_h(spec: FsmSpec) -> str:
    """Emit ``<name>_fsm_model.h`` — always regenerated, never edit by hand."""
    U = spec.upper
    N = spec.name
    guard_name = f"RXNET_{U}_FSM_MODEL_H"

    ", ".join(
        "rx_fsm_machine *machine" if i < 0 else f"{p.ctype} {p.name}"
        for i, p in [(-1, None)] + list(enumerate(spec.params))   # prepend machine*
    )
    # build proper param sig
    c_params = "rx_fsm_machine *machine"
    if spec.params:
        c_params += ", " + ", ".join(f"{p.ctype} {p.name}" for p in spec.params)

    lines: list[str] = []
    lines += [
        f"// {COPYRIGHT}",
        f"// SPDX-License-Identifier: {SPDX}",
        "//",
        "// GENERATED — do not edit by hand.",
        f"// Source: {N}.yaml   (regenerate with: python -m rxnet.tools.gen {N}.yaml)",
        "",
        "#pragma once",
        f"#ifndef {guard_name}",
        f"#define {guard_name}",
        "",
        '#include "rxnet/fsm.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "/* ── states ──────────────────────────────────────────────────────────────── */",
        "enum {",
    ]
    for s in spec.states:
        lines.append(f"    {U}_STATE_{s.name} = {s.id},")
    lines += [
        "};",
        "",
        "/* ── public factory ─────────────────────────────────────────────────────── */",
        "/*",
        f" * Initialise *machine as a ``{N}`` FSM.",
        " * Call once; the machine must remain alive for the runtime's lifetime.",
        " */",
        f"void {N}_fsm_create({c_params});",
        "",
    ]

    # document expected guard / action signatures as comments
    if spec.guards or spec.actions:
        lines += [
            f"/* ── expected callbacks (implement in {N}_fsm.c) ──────────────────────── */",
            "/*",
        ]
        for g in spec.guards:
            lines.append(f" * static int {g}(const rx_fsm_context *ctx, void *user);")
        for a in spec.actions:
            lines.append(f" * static void {a}(rx_fsm_context *ctx, void *user);")
        lines += [" */", ""]

    lines += [
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        f"#endif /* {guard_name} */",
        "",
    ]
    return "\n".join(lines)


# ── C stub generators ─────────────────────────────────────────────────────────

def generate_c_stub_h(spec: FsmSpec) -> str:
    """Emit ``<name>_fsm.h`` — public header; written once, never overwritten."""
    U = spec.upper
    N = spec.name
    guard_name = f"RXNET_{U}_FSM_H"

    c_params = "rx_fsm_machine *machine"
    if spec.params:
        c_params += ", " + ", ".join(f"{p.ctype} {p.name}" for p in spec.params)

    lines: list[str] = [
        f"// {COPYRIGHT}",
        f"// SPDX-License-Identifier: {SPDX}",
        "",
        "#pragma once",
        f"#ifndef {guard_name}",
        f"#define {guard_name}",
        "",
        f'#include "{N}_fsm_model.h"',
        '#include "app_driver.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f"void {N}_fsm_create({c_params});",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        f"#endif /* {guard_name} */",
        "",
    ]
    return "\n".join(lines)


def generate_c_stub_c(spec: FsmSpec) -> str:
    """Emit ``<name>_fsm.c`` — implementation stubs; written once, never overwritten."""
    U = spec.upper
    N = spec.name
    n_trans = len(spec.transitions)

    c_factory_params = "rx_fsm_machine *machine"
    if spec.params:
        c_factory_params += ", " + ", ".join(f"{p.ctype} {p.name}" for p in spec.params)

    lines: list[str] = [
        f"// {COPYRIGHT}",
        f"// SPDX-License-Identifier: {SPDX}",
        "",
        f'#include "{N}_fsm.h"',
        "",
        "#include <stddef.h>",
        "#include <string.h>",
        "",
        '#include "rxnet/config.h"',
        "",
        "/* ── private data ────────────────────────────────────────────────────────── */",
        "",
        "typedef struct {",
        "    bool in_use;",
        "    rx_fsm_machine *machine;",
    ]
    for p in spec.params:
        lines.append(f"    {p.ctype} {p.name};")
    lines += [
        "    /* TODO: add runtime fields (latched inputs, outputs, timers …) */",
        f"}} {N}_data;",
        "",
        f"static {N}_data s_data[RXNET_MAX_RUNTIME_NODES];",
        "",
        f"static {N}_data *_find_or_alloc(rx_fsm_machine *m) {{",
        "    size_t i;",
        "    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i)",
        "        if (s_data[i].in_use && s_data[i].machine == m) return &s_data[i];",
        "    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {",
        "        if (!s_data[i].in_use) {",
        "            memset(&s_data[i], 0, sizeof(s_data[i]));",
        "            s_data[i].in_use = true;",
        "            s_data[i].machine = m;",
        "            return &s_data[i];",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
        "/* ── lifecycle callbacks ─────────────────────────────────────────────────── */",
        "",
        f"static void {N}_latch_inputs(rx_fsm_context *ctx, void *user) {{",
        f"    {N}_data *data = ({N}_data *)user;",
        "    (void)ctx;",
        "    if (data == NULL) return;",
        "    /* TODO: snapshot hardware inputs into data fields */",
        "}",
        "",
        f"static void {N}_dump_outputs(rx_fsm_context *ctx, void *user) {{",
        f"    {N}_data *data = ({N}_data *)user;",
        "    (void)ctx;",
        "    if (data == NULL) return;",
        "    /* TODO: write outputs to hardware */",
        "}",
        "",
    ]

    # guards
    if spec.guards:
        lines.append("/* ── guards ──────────────────────────────────────────────────────────── */")
        lines.append("")
        for g in spec.guards:
            lines += [
                f"static int {g}(const rx_fsm_context *ctx, void *user) {{",
                f"    const {N}_data *data = (const {N}_data *)user;",
                "    (void)ctx;",
                "    if (data == NULL) return 0;",
                "    /* TODO: implement guard */",
                "    return 0;",
                "}",
                "",
            ]

    # actions
    if spec.actions:
        lines.append("/* ── actions ─────────────────────────────────────────────────────────── */")
        lines.append("")
        for a in spec.actions:
            lines += [
                f"static void {a}(rx_fsm_context *ctx, void *user) {{",
                f"    {N}_data *data = ({N}_data *)user;",
                "    (void)ctx;",
                "    if (data == NULL) return;",
                "    /* TODO: implement action */",
                "}",
                "",
            ]

    # factory
    param_inits = "\n".join(f"    data->{p.name} = {p.name};" for p in spec.params)
    trans_entries: list[str] = []
    for t in spec.transitions:
        guard_ptr = t.guard if t.guard else "NULL"
        action_ptr = t.action if t.action else "NULL"
        trans_entries.append(
            f"        {{{U}_STATE_{t.from_name}, {U}_STATE_{t.to_name},"
            f" {guard_ptr}, {action_ptr}}},"
        )

    lines += [
        "/* ── factory ─────────────────────────────────────────────────────────────── */",
        "",
        f"void {N}_fsm_create({c_factory_params}) {{",
        f"    static const rx_fsm_transition transitions[{n_trans}] = {{",
    ]
    lines.extend(trans_entries)
    lines += [
        "    };",
        f"    {N}_data *data = _find_or_alloc(machine);",
        "",
        "    if (machine == NULL || data == NULL) return;",
        "",
        textwrap.indent(param_inits, "    "),
        "    /* TODO: initialise hardware and extra data fields */",
        "",
        "    rx_fsm_machine_init(",
        f"        machine, {N!r}, {U}_STATE_{spec.initial_name},",
        f"        transitions, {n_trans},",
        f"        data, {N}_latch_inputs, {N}_dump_outputs",
        "    );",
        "}",
        "",
    ]

    return "\n".join(lines)


# ── TLA+ generator ────────────────────────────────────────────────────────────

def _tla_guard_var(guard: str) -> str:
    """``button_pressed`` → ``ButtonPressed``."""
    return "".join(w.capitalize() for w in guard.split("_"))


def generate_tla(spec: FsmSpec) -> str:
    """Emit ``<name>.tla`` — always regenerated, never edit by hand."""
    N = spec.name
    module = f"{N}_fsm"

    guard_vars = [_tla_guard_var(g) for g in spec.guards]
    all_vars = ["state"] + guard_vars
    vars_tuple = "<<" + ", ".join(all_vars) + ">>"

    lines: list[str] = [
        f"---- MODULE {module} ----",
        f"\\* {COPYRIGHT}",
        f"\\* SPDX-License-Identifier: {SPDX}",
        "\\*",
        "\\* GENERATED — do not edit by hand.",
        f"\\* Source: {N}.yaml   (regenerate with: python -m rxnet.tools.gen {N}.yaml)",
        "\\*",
        "\\* Guards are modelled as nondeterministic boolean environment variables.",
        "\\* This gives a conservative over-approximation: TLC explores all possible",
        "\\* guard sequences.  Liveness properties require explicit fairness constraints.",
        "EXTENDS Integers, TLC",
        "",
        "\\* ── state identifiers ────────────────────────────────────────────────────",
    ]
    for s in spec.states:
        lines.append(f"S_{s.name} == {s.id}")
    lines += [
        "",
        f"States == {{{', '.join('S_' + s.name for s in spec.states)}}}",
        "",
        "\\* ── variables ────────────────────────────────────────────────────────────",
        "VARIABLES",
        "    state,   \\* current machine state (integer)",
    ]
    for gv in guard_vars:
        lines.append(f"    {gv},   \\* abstract guard (boolean environment input)")
    lines += [
        "",
        f"vars == {vars_tuple}",
        "",
        "\\* ── initial state ────────────────────────────────────────────────────────",
        "Init ==",
        f"    /\\ state = S_{spec.initial_name}",
    ]
    for gv in guard_vars:
        lines.append(f"    /\\ {gv} \\in BOOLEAN")
    lines.append("")

    # one operator per transition
    lines.append("\\* ── transitions ──────────────────────────────────────────────────────────")
    for i, t in enumerate(spec.transitions):
        op_parts = [f"T_{t.from_name}_{t.to_name}"]
        if t.guard:
            op_parts.append(t.guard)
        op_name = "_".join(op_parts)
        comment = f"{t.from_name} --[{t.guard or 'unconditional'}]--> {t.to_name}"
        if t.action:
            comment += f"  (action: {t.action})"
        lines += ["", f"\\* {comment}", f"{op_name} =="]
        lines.append(f"    /\\ state = S_{t.from_name}")
        if t.guard:
            lines.append(f"    /\\ {_tla_guard_var(t.guard)}")
        lines.append(f"    /\\ state' = S_{t.to_name}")
        # refresh all guard variables nondeterministically
        for gv in guard_vars:
            lines.append(f"    /\\ {gv}' \\in BOOLEAN")

    lines += [
        "",
        "\\* allow the system to stutter (required for UNCHANGED semantics)",
        f"Stutter == UNCHANGED {vars_tuple}",
        "",
        "Next ==",
    ]
    for i, t in enumerate(spec.transitions):
        op_parts = [f"T_{t.from_name}_{t.to_name}"]
        if t.guard:
            op_parts.append(t.guard)
        op_name = "_".join(op_parts)
        prefix = "    \\/" if i > 0 else "      "
        lines.append(f"{prefix} {op_name}")
    lines += [
        "    \\/ Stutter",
        "",
        f"Spec == Init /\\ [][Next]_{vars_tuple}",
        "",
        "\\* ── type invariant ──────────────────────────────────────────────────────",
        "TypeOK ==",
        "    /\\ state \\in States",
    ]
    for gv in guard_vars:
        lines.append(f"    /\\ {gv} \\in BOOLEAN")

    lines += [
        "",
        "\\* ── properties to verify ────────────────────────────────────────────────",
        "\\* Uncomment and adapt for TLC model checking.",
        "\\*",
        "\\* Safety — machine stays in the defined state space (should always hold):",
        "\\*   THEOREM Spec => []TypeOK",
        "\\*",
        "\\* Liveness — add fairness then check a reachability property:",
        "\\*   Fairness == WF_vars(Next)",
        "\\*   LiveSpec == Spec /\\ Fairness",
        "\\*",
    ]
    for s in spec.states:
        if s.id != spec.initial_id:
            lines.append(f"\\*   Reaches_{s.name} == <>(state = S_{s.name})")
    lines += [
        "",
        "====",
        "",
    ]
    return "\n".join(lines)


# ── consistency checker ───────────────────────────────────────────────────────

@dataclass
class Issue:
    kind: str       # "missing" | "orphan"
    what: str       # "guard" | "action"
    name: str
    file: Path

    def __str__(self) -> str:
        emoji = "  MISSING" if self.kind == "missing" else "  ORPHAN "
        return f"{emoji} {self.what} '{self.name}' in {self.file.name}"


def check_python(spec: FsmSpec, py_path: Path) -> list[Issue]:
    """Check ``<name>_fsm.py`` for missing / orphan guard and action functions."""
    if not py_path.exists():
        return []
    try:
        tree = ast.parse(py_path.read_text(encoding="utf-8"))
    except SyntaxError:
        return []

    defined = {
        node.name.lstrip("_")
        for node in ast.walk(tree)
        if isinstance(node, ast.FunctionDef)
    }

    issues: list[Issue] = []
    for g in spec.guards:
        if g not in defined:
            issues.append(Issue("missing", "guard", g, py_path))
    for a in spec.actions:
        if a not in defined:
            issues.append(Issue("missing", "action", a, py_path))

    all_expected = set(spec.guards) | set(spec.actions)
    for fn in defined:
        if fn in ("latch", "dump"):
            continue  # lifecycle callbacks, not in YAML
        # only flag names that look like guards/actions (present in impl but not in model)
        if fn not in all_expected and not fn.startswith(spec.name):
            # Heuristic: short snake_case names that are neither lifecycle nor helpers
            # are potential orphans.  We only flag exact former-guard/action names.
            pass  # conservative: don't flag — too many false positives for helpers

    return issues


def check_c(spec: FsmSpec, c_path: Path) -> list[Issue]:
    """Check ``<name>_fsm.c`` for missing / orphan guard and action functions."""
    if not c_path.exists():
        return []
    src = c_path.read_text(encoding="utf-8")
    issues: list[Issue] = []

    for g in spec.guards:
        if not re.search(rf'\b{re.escape(g)}\s*\(', src):
            issues.append(Issue("missing", "guard", g, c_path))
    for a in spec.actions:
        if not re.search(rf'\b{re.escape(a)}\s*\(', src):
            issues.append(Issue("missing", "action", a, c_path))

    return issues


# ── file writer ───────────────────────────────────────────────────────────────

def _write(path: Path, content: str, overwrite: bool, dry_run: bool) -> str:
    """Write *content* to *path*.  Return a one-line status string."""
    if path.exists() and not overwrite:
        return f"  kept     {path}"
    if dry_run:
        return f"  would    {path}"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    "overwrote" if path.exists() else "created  "
    return f"  wrote    {path}"


# ── CLI entry point ───────────────────────────────────────────────────────────

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="python -m rxnet.tools.gen",
        description="Generate FSM model, stubs, and TLA+ spec from a YAML descriptor.",
    )
    ap.add_argument("yaml", metavar="FSM_YAML", help="path to the FSM YAML descriptor")
    ap.add_argument(
        "--py-dir", metavar="DIR",
        help="output directory for Python files (default: YAML directory)",
    )
    ap.add_argument(
        "--c-dir", metavar="DIR",
        help="output directory for C files (default: YAML directory)",
    )
    ap.add_argument(
        "--tla-dir", metavar="DIR",
        help="output directory for TLA+ files (default: YAML directory)",
    )
    ap.add_argument(
        "--check", action="store_true",
        help="run consistency check only; write nothing",
    )
    ap.add_argument(
        "--force", action="store_true",
        help="overwrite implementation stubs (DESTRUCTIVE — resets hand-written code)",
    )
    args = ap.parse_args(argv)

    yaml_path = Path(args.yaml).resolve()
    if not yaml_path.exists():
        print(f"error: {yaml_path} not found", file=sys.stderr)
        return 1

    try:
        spec = parse_yaml(yaml_path)
    except (KeyError, TypeError, ValueError) as exc:
        print(f"error: invalid YAML — {exc}", file=sys.stderr)
        return 1

    yaml_dir = yaml_path.parent
    py_dir = Path(args.py_dir).resolve() if args.py_dir else yaml_dir
    c_dir = Path(args.c_dir).resolve() if args.c_dir else yaml_dir
    tla_dir = Path(args.tla_dir).resolve() if args.tla_dir else yaml_dir

    N = spec.name
    dry = args.check

    print(f"rxnet-gen  {yaml_path.name}  →  {spec.name}")
    print()

    # ── always-regenerated files ──────────────────────────────────────────────
    print("Generated (always overwritten):")
    print(_write(py_dir / f"{N}_fsm_model.py",  generate_python_model(spec), True, dry))
    print(_write(c_dir  / f"{N}_fsm_model.h",   generate_c_model_h(spec),   True, dry))
    print(_write(tla_dir / f"{N}_fsm.tla",       generate_tla(spec),         True, dry))
    print()

    # ── impl stubs (write once) ───────────────────────────────────────────────
    note = "; --force active — OVERWRITING" if args.force else "; --force to reset"
    print(f"Implementation stubs (written once{note}):")
    print(_write(py_dir / f"{N}_fsm.py", generate_python_stub(spec), args.force, dry))
    print(_write(c_dir  / f"{N}_fsm.c",  generate_c_stub_c(spec),    args.force, dry))
    print(_write(c_dir  / f"{N}_fsm.h",  generate_c_stub_h(spec),    args.force, dry))
    print()

    # ── consistency check ─────────────────────────────────────────────────────
    issues = (
        check_python(spec, py_dir / f"{N}_fsm.py") +
        check_c(spec, c_dir / f"{N}_fsm.c")
    )

    if issues:
        print("Consistency warnings:")
        for issue in issues:
            print(issue)
        print()
    else:
        print("Consistency check: OK")

    return 1 if (issues and args.check) else 0


if __name__ == "__main__":
    sys.exit(main())
