# Design: rxnet.tools.extract_c — FSM extractor from C source

**Date:** 2026-05-20  
**Status:** approved

## Problem

`gen.py` generates TLA+ specs and diagrams starting from a YAML descriptor. When FSMs are written directly in C (without a YAML), there is no way to produce TLA+ or visual diagrams from the implementation. This tool fills that gap.

## Approach

Run `clang -Xclang -ast-dump=json -fsyntax-only` as a subprocess on the C source file, parse the resulting JSON, extract the FSM topology into an `FsmSpec`, and emit one or more output formats.

No extra Python dependencies beyond the stdlib and the existing `gen.py` dataclasses. Clang must be installed on the host.

## Architecture

```
C source file
     │
     ▼
clang -ast-dump=json          (subprocess, extra -I flags forwarded)
     │
     ▼
ast_to_spec(json) → FsmSpec   (same frozen dataclass as gen.py)
     │
     ├── --tla     → generate_tla(spec)       (reused from gen.py)
     ├── --dot     → generate_dot(spec)       (new, ~15 lines)
     ├── --yaml    → generate_yaml(spec)      (new, ~20 lines)
     └── --mermaid → generate_mermaid(spec)   (new, ~15 lines)
```

All output goes to **stdout** so the tool composes naturally with pipes and Makefile rules.

## AST extraction — three targeted patterns

The extractor ignores everything in the JSON except these three node kinds:

1. **`EnumConstantDecl`** with name matching `*_STATE_*`  
   → builds `{name: int_value}` lookup table for state names

2. **`VarDecl`** named `transitions` with `qualType` containing `rx_fsm_transition [`  
   → each child `InitListExpr` yields one `FsmTransition(from_id, to_id, guard, action)`  
   Guard/action names come from the `referencedDecl.name` of the deepest `DeclRefExpr`; a `NullExpr` or cast of `0` means `None`.

3. **`CallExpr`** whose first child resolves to `rx_fsm_machine_init`  
   → argument 1 is a `StringLiteral` → machine name  
   → argument 2 is a `DeclRefExpr` → initial state name

## New file

`python/rxnet/tools/extract_c.py`

Public surface:

```python
def ast_to_spec(ast: dict, prefix_hint: str | None = None) -> FsmSpec: ...
def generate_dot(spec: FsmSpec) -> str: ...
def generate_yaml(spec: FsmSpec) -> str: ...
def generate_mermaid(spec: FsmSpec) -> str: ...
```

`generate_tla` is imported from `gen.py` (no duplication).

## CLI

```
python -m rxnet.tools.extract_c <FILE.c> [CLANG_FLAGS...] [--tla] [--dot] [--yaml] [--mermaid]
```

- Extra positional/flag arguments that start with `-` (e.g. `-I`, `-D`) are forwarded verbatim to clang.
- At least one output flag is required; multiple flags emit all selected formats separated by `\n---\n`.
- Exit 0 on success, 1 on clang error or parse failure with a message to stderr.

### Examples

```bash
# TLA+ to stdout
python -m rxnet.tools.extract_c light_fsm.c -I../../include --tla

# DOT piped to Graphviz
python -m rxnet.tools.extract_c light_fsm.c -I../../include --dot | dot -Tsvg -o light.svg

# YAML descriptor (round-trip with gen.py)
python -m rxnet.tools.extract_c light_fsm.c -I../../include --yaml > light.yaml

# Mermaid
python -m rxnet.tools.extract_c light_fsm.c -I../../include --mermaid
```

## Makefile integration

Added to `c/Makefile` (root-level) and to each example subdirectory Makefile:

```makefile
RXNET_PY  = python -m rxnet.tools.extract_c
RXNET_INC = -I$(RXNET_ROOT)/include

# Pattern rules — work for any *_fsm.c in scope
%.tla: %_fsm.c
	$(RXNET_PY) $< $(RXNET_INC) --tla > $@

%.dot: %_fsm.c
	$(RXNET_PY) $< $(RXNET_INC) --dot > $@

%.svg: %.dot
	dot -Tsvg $< -o $@

%.pdf: %.dot
	dot -Tpdf $< -o $@

# Convenience aggregate target
diagrams: light.svg light.pdf   # (or auto, blink, etc. per example)
```

PDF and SVG are both default targets in each example.

## Examples updated

- `c/examples/fsm/01-light/`
- `c/examples/fsm/02-auto/`
- `c/examples/fsm/03-blink/`

Each gets a `Makefile` with the pattern rules above and a `diagrams` target that produces `.svg` and `.pdf` from the local `*_fsm.c`.

## Output format details

### DOT
Standard Graphviz digraph. States as circles, initial state marked with a point node, transitions labelled `guard / action` (or just `guard` if no action, or `—` if unconditional).

### YAML
Round-trippable with `gen.py`. Includes `name`, `states`, `initial`, `transitions` (with `guard` and `action` fields). `params` section is omitted (cannot be recovered from the C AST without deeper analysis).

### Mermaid
`stateDiagram-v2` block, compatible with GitHub Markdown rendering.

### TLA+
Identical output to `gen.py`'s `generate_tla` — same module format, same guard abstraction model.

## Constraints

- Requires `clang` on `$PATH`.
- Requires include paths for rxnet headers (`rxnet/fsm.h`). The Makefile provides these via `RXNET_INC`.
- Only handles one machine per C file (the first `rx_fsm_machine_init` call found). Files with multiple machines are out of scope.
- `params` (factory function arguments) are not recovered; the YAML output omits the `params` section.
