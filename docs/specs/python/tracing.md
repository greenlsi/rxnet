# Design Document: rxnet — Python Tracing Subsystem

## Overview

This document specifies the optional tracing subsystem for the Python implementation of `rxnet`.  
It covers event taxonomy, buffer design, API, download protocol, binary wire format, and visualization.

**Fundamental constraint**: when tracing is not activated, **zero overhead** is incurred — no
allocations, no None-checks in hot paths, no timestamp calls. The production code path is
identical to a build without the tracing subsystem.

---

## Goals

| Priority | Goal |
|---|---|
| **G1** | Zero overhead when not in use |
| **G2** | Machine-level trace: activations, transitions, worst-case response time |
| **G3** | User events: arbitrary label + integer value, inserted from application code |
| **G4** | Phase-level trace: duration of latch / evaluate / commit / dump (optional, for teaching) |
| **G5** | Download from a running process via HTTP — dev tool pulls, target pushes nothing |
| **G6** | Visualization in a web browser with no installation: [ui.perfetto.dev](https://ui.perfetto.dev) |
| **G7** | Common binary wire format with the C implementation (same decoder tool) |

---

## Event Taxonomy

### Event types

```
TICK_START       slot N begins; which nodes are active this slot
NODE_START       node X begins latch phase (activation time)
NODE_END         node X finishes dump phase (completion time)
PHASE_START      optional: one of { latch, evaluate, commit, dump } begins for node X
PHASE_END        optional: corresponding phase ends
FSM_TRANSITION   machine X: state A → state B (deduced from commit delta)
PN_FIRING        net X: transition T fires (deduced from commit delta)
USER             user-defined: label_id (uint16) + value (uint16)
```

All events carry a nanosecond-precision monotonic timestamp relative to `t0` (the instant
the tracer is attached).

### Derived metrics (computed by the decoder, not stored)

- **Response time** of node X on tick T = `NODE_END.t_ns − NODE_START.t_ns`
- **WCRT** (worst-case response time) of node X = `max(response_time)` over the trace window
- **Phase duration** = `PHASE_END.t_ns − PHASE_START.t_ns`
- **Activation jitter** = deviation of `NODE_START.t_ns` from the ideal period multiple

### What is NOT stored per-event (recovered from the name table)

State names, place names, transition names, node names — stored once in the trace header,
not repeated per event.

---

## Zero-Overhead Design

### The problem with `if tracer is not None`

A None-check in every phase of every node per tick:

```
4 nodes × 5 phases × 100 Hz = 2000 checks/sec ≈ 10 µs/sec overhead
```

This is negligible in absolute terms, but the architectural goal is **structural zero
overhead**: the production code path must be identical to code that was never aware of
tracing.

### Solution: node wrapper pattern

The tracer operates exclusively through **transparent node wrappers**. The `Runtime._entries`
list holds `_NodeEntry(node=..., period_us=...)`. When the tracer is attached, it replaces
`entry.node` with a `_TracedNode` proxy that:

1. Delegates all four phase methods to the original node
2. Records timestamps before and after each call
3. Records FSM/PN state deltas by snapshotting before and after commit

When no tracer is attached, `_entries` contains the original Machine/Net objects — the runtime
loop executes them with **zero extra code**.

```
No tracer:
    _entries = [_NodeEntry(node=machine_a), _NodeEntry(node=blink_b), ...]
    tick() → machine_a.latch_inputs(ctx)   ← unmodified path

Tracer attached:
    _entries = [_NodeEntry(node=_TracedMachine(machine_a, tracer)), ...]
    tick() → _TracedMachine.latch_inputs(ctx) → machine_a.latch_inputs(ctx)
                                               → tracer._record_phase_end(...)
```

Neither `Runtime`, `Machine`, nor `Net` gain any tracer-related fields or checks.

### The `build()` constraint

`rt.build()` constructs `_slots: list[list[Node]]` from `_entries`. The tracer must call
`attach(rt)` **before** `rt.build()` (equivalently, before the first `tick()`). After
attachment, `build()` will see the wrappers and include them in the slot lists.

If `attach()` is called after `build()`, the tracer calls `rt.build()` to force a rebuild.

### Detaching the tracer

`tracer.detach(rt)` unwraps all `_TracedNode` entries and calls `rt.build()` to restore the
original slot lists. After detach the system returns to full production mode.

---

## Buffer Design

### Ring buffer

```python
# Pre-allocated at Tracer construction time — no allocation in the hot path
_events: array  # array.array('Q', [0] * (max_events * WORDS_PER_EVENT))
_head: int      # next write index (mod max_events)
_count: int     # events stored (saturates at max_events)
_dropped: int   # events discarded due to overflow
_t0_ns: int     # time.monotonic_ns() at attach()
```

Events are packed into a flat `array.array('Q')` (unsigned 64-bit words) using `struct.pack_into`.
This avoids per-event Python object allocation in the recording path.

### Event layout (16 bytes = 2 × uint64)

```
Word 0 (8 bytes):
  [63:0]  t_ns       uint64  nanoseconds from t0

Word 1 (8 bytes):
  [63:56] type       uint8   event type (see taxonomy)
  [55:48] node_id    uint8   node index (0–255)
  [47:32] a          uint16  from_state / slot / phase_id / label_id
  [31:16] b          uint16  to_state / user_value / 0
  [15:0]  c          uint16  spare / dropped_count snapshot
```

**Maximum events vs memory:**

| max_events | RAM |
|---|---|
| 4 096 | 64 KB |
| 16 384 | 256 KB |
| 65 536 | 1 MB |

64 KB (4096 events) is sufficient for ~40 seconds at 100 Hz with 4 nodes and all event
types enabled.

### Overflow policy

When the buffer is full, new events overwrite the oldest (sliding window). The `_dropped`
counter records how many overwrites occurred. The decoder marks a gap in the Perfetto
timeline wherever overflow is detected.

---

## API

### `Tracer` construction

```python
from rxnet.trace import Tracer

tracer = Tracer(
    max_events  = 4096,       # ring buffer capacity
    phases      = False,      # record PHASE_START/END (default: off)
)
```

`phases=False` (default) records only `NODE_START`, `NODE_END`, `FSM_TRANSITION`,
`PN_FIRING`, `TICK_START`, and `USER` events. `phases=True` additionally records
per-phase boundary events — useful for teaching, higher event rate.

### Attaching to a runtime

```python
tracer.attach(rt)    # call before ce.run() or te.run()
tracer.detach(rt)    # restore production path
```

One tracer can be attached to multiple runtimes. Each runtime gets independent node
wrappers; all events go to the same ring buffer, distinguished by `node_id`.

### Naming (optional but recommended)

```python
tracer.name_node(machine_a,  "light_a")
tracer.name_node(blink_b,    "blink_b")
tracer.name_node(auto_c,     "auto_c")

# FSM state names (int → string)
tracer.name_fsm_states(machine_a, {0: "OFF", 1: "ON"})
tracer.name_fsm_states(blink_b,   {0: "OFF", 1: "X1", 2: "X2"})

# PN place names
tracer.name_pn_places(light_net, {0: "IDLE", 1: "REQUEST", 2: "ON"})

# PN transition names
tracer.name_pn_transitions(light_net, {0: "press", 1: "release"})
```

If names are not provided, the decoder uses numeric IDs (`state_1`, `place_2`, etc.).

### Recording user events

```python
# From application code (e.g. a CLI command handler or an ISR equivalent):
tracer.user(label="button_a", value=1)
tracer.user(label="timeout",  value=0)
```

`label` strings are registered in the name table on first use; only a `label_id` integer is
stored in the ring buffer per event.

### Export and download

```python
# Dump to file (blocks briefly to drain buffer snapshot)
tracer.export(path="trace.bin")           # binary wire format
tracer.export(path="trace.json", fmt="perfetto")  # Perfetto JSON directly

# HTTP endpoint (daemon thread) — dev tool downloads from here
tracer.serve(host="0.0.0.0", port=7777)
# GET /trace        → binary blob (wire format), resets dropped counter
# GET /trace.json   → Perfetto JSON directly
# GET /status       → JSON: {events, dropped, t_elapsed_s}
# DELETE /trace     → clears the ring buffer
```

---

## Diagram Generation

Both FSM and PN diagrams are rendered using **Graphviz DOT** — one tool, consistent visual
style, no extra installation.

| Model | DOT convention |
|---|---|
| FSM | `digraph`; states = `shape=circle`; initial state = invisible `point` node |
| PN | `digraph`; places = `shape=circle` with token count in label; transitions = `shape=rectangle` |

The `rxnet.diagram` module generates DOT source from topology already present in `Machine`
and `Net` objects — no separate registration:

```python
from rxnet.diagram import fsm_to_dot, pn_to_dot

dot = fsm_to_dot(machine)   # reads machine.transitions, machine.state_names
dot = pn_to_dot(net)        # reads net.transitions, net.place_names
```

### Metadata fields (new optional fields on existing dataclasses)

```python
# fsm.py — Transition gains one optional field
@dataclass(frozen=True, slots=True)
class Transition:
    from_state: int
    to_state:   int
    guard:      Guard | None = None
    action:     Action | None = None
    label:      str | None   = None   # arc label in diagram and trace

# fsm.py — Machine gains one optional field
@dataclass(slots=True)
class Machine:
    ...
    state_names: dict[int, str] | None = None   # {0: "OFF", 1: "ON"}

# pn.py — Net gains two optional fields
@dataclass(slots=True)
class Net:
    ...
    place_names:      dict[int, str] | None = None   # {0: "IDLE", 1: "ON"}
    transition_names: list[str]      | None = None   # ["press", "release"]
```

If names are absent the decoder falls back to `state_0`, `P0`, `T0`, etc. These are the
**only changes** to existing model code.

---

## Download and Decoder Tool

### `rxnet-trace` CLI

A standalone Python tool (no GUI, runs on the dev machine):

```bash
# From HTTP endpoint — generates HTML report and opens browser
python -m rxnet.tools.trace http://192.168.1.10:7777 --report report.html --open

# From binary file (e.g. scp'd from target)
python -m rxnet.tools.trace trace.bin --report report.html --open

# Perfetto JSON only (for manual upload to ui.perfetto.dev)
python -m rxnet.tools.trace http://target:7777 --perfetto trace.json

# Stats summary to stdout
python -m rxnet.tools.trace http://target:7777 --stats
```

### HTML report — zero installation

The decoder produces a **standalone HTML file** that renders in any modern browser with no
installation on the Mac. It embeds:

1. **DOT diagrams** (FSM and PN) — rendered client-side via `@hpcc-js/wasm` (Graphviz
   compiled to WebAssembly, loaded from CDN or bundled for offline use)
2. **WCRT table** — worst-case response time per node, computed from `NODE_START`/`NODE_END`
   pairs
3. **"Open in Perfetto"** button — serves the Perfetto JSON on a local port and opens
   `ui.perfetto.dev`

```html
<!-- One CDN script for all DOT rendering — no graphviz install needed -->
<script src="https://cdn.jsdelivr.net/npm/@hpcc-js/wasm@2/dist/graphviz.umd.js"></script>
```

For offline use (target on a local network without internet access), `--offline` bundles
the WASM script directly into the HTML.

### Perfetto track hierarchy

```
Process: rxnet / runtime_0
  Thread: scheduler          ← TICK_START instants
  Thread: light_a
    slice: [    node activation    ]
    slice:   [latch][eval][commit][dump]  ← only if phases=True
    instant: OFF → ON
  Counter: light_a / response_time_us
  Counter: light_a / wcrt_us
  Counter: light_a / state
  Thread: blink_b
    ...
  Thread: auto_c
    ...
  Thread: [user events]
    instant: button_a=1
    instant: timeout=0
```

### What the decoder produces from each event type

| rxnet event | Output |
|---|---|
| `TICK_START` | Perfetto instant on "scheduler" track |
| `NODE_START`…`NODE_END` | Perfetto complete slice (duration = response time) |
| `PHASE_START`…`PHASE_END` | Nested slices within the node slice |
| `FSM_TRANSITION` | Perfetto instant: `"OFF → ON"`; state counter update |
| `PN_FIRING` | Perfetto instant: `"press fired"` |
| `USER` | Perfetto instant on "user events" track |
| `NODE_START`/`NODE_END` pairs | WCRT counter (running maximum) in HTML table |
| Machine topology | DOT FSM diagram in HTML report |
| Net topology | DOT PN diagram in HTML report |

---

## Binary Wire Format

Shared with the C implementation. The decoder tool handles both.

### Header (32 bytes fixed)

```
[0:4]    magic     = 0x52 0x58 0x4E 0x54  ("RXNT")
[4]      version   = 1
[5]      event_sz  = 16
[6]      lang      = 0x50  ('P' = Python, 'C' = C)
[7]      flags     bit 0: has_phases
                   bit 1: has_names
[8:16]   t0_ns     uint64 LE  (wall clock at first event, for display)
[16:20]  n_events  uint32 LE  (events in this dump, ≤ max_events)
[20:24]  n_dropped uint32 LE
[24:32]  reserved  (zeros)
```

### Name table (variable, after header)

```
[1]  node_count
For each node:
  [1]  name_len
  [N]  name bytes (UTF-8)

[1]  fsm_table_count   (number of FSM nodes with state names)
For each FSM node:
  [1]  node_id
  [1]  state_count
  For each state:
    [1]  state_id
    [1]  name_len
    [N]  name bytes

[1]  label_count        (user event labels)
For each label:
  [1]  label_id
  [1]  name_len
  [N]  name bytes
```

### Event array (fixed, n_events × 16 bytes)

```
Per event (16 bytes):
  [0:8]   t_ns    uint64 LE  nanoseconds from t0
  [8]     type    uint8      event type
  [9]     node_id uint8
  [10:12] a       uint16 LE  meaning depends on type (see below)
  [12:14] b       uint16 LE
  [14:16] c       uint16 LE  (spare / overflow marker)
```

**Field semantics by type:**

| type | a | b | c |
|---|---|---|---|
| `TICK_START` | slot_id | active_node_count | 0 |
| `NODE_START` | slot_id | 0 | 0 |
| `NODE_END` | slot_id | 0 | 0 |
| `PHASE_START` | phase_id (0–3) | 0 | 0 |
| `PHASE_END` | phase_id | 0 | 0 |
| `FSM_TRANSITION` | from_state | to_state | transition_index |
| `PN_FIRING` | transition_index | 0 | 0 |
| `USER` | label_id | value | 0 |

Phase IDs: 0=latch, 1=evaluate, 2=commit, 3=dump.

---

## Implementation Notes

### `_TracedNode` internals

```python
class _TracedMachine:
    """Transparent wrapper — same Node protocol, adds timing and state tracking."""
    __slots__ = ("_machine", "_tracer", "_node_id")

    def latch_inputs(self, ctx):
        self._tracer._record(NODE_START, self._node_id, ...)
        self._machine.latch_inputs(ctx)

    def evaluate(self, ctx):
        self._machine.evaluate(ctx)

    def commit(self, ctx):
        prev = self._machine.state
        self._machine.commit(ctx)
        if self._machine.state != prev:
            self._tracer._record(FSM_TRANSITION, self._node_id,
                                 a=prev, b=self._machine.state)

    def dump_outputs(self, ctx):
        self._machine.dump_outputs(ctx)
        self._tracer._record(NODE_END, self._node_id, ...)
```

`_TracedNet` works analogously, snapshotting `net._fire_flags` after `commit()` to detect
which transitions fired.

### Thread safety of the ring buffer

- `ThreadExecutive` runs node phases from multiple threads simultaneously.
- The ring buffer write (one 16-byte event) must be atomic or protected.
- Use `threading.Lock` for the head pointer; keep the critical section minimal
  (pack the 16 bytes into a local `bytes` object before acquiring the lock).
- `serve()` acquires the same lock to snapshot the buffer for export.

### `time.monotonic_ns()` resolution

On macOS, `time.monotonic_ns()` has ~40–100 ns resolution (hardware timer).  
On Linux, ~1 ns (VDSO `clock_gettime(CLOCK_MONOTONIC)`).  
For 10 ms periods the resolution is more than adequate.

---

## Open Questions

| # | Question | Impact |
|---|---|---|
| OQ-1 | **Multi-device timestamp alignment**: how do we align traces from two separate processes / devices? Options: NTP sync, shared epoch via HTTP handshake, or PTP. | Needed for multi-node system tracing; out of scope for v1. |
| OQ-2 | **`phases=True` overhead**: with 4 nodes × 4 phases × 100 Hz, recording 1600 extra events/sec. Is this acceptable for teaching scenarios? | Only enabled explicitly; not a production concern. |
| OQ-3 | **PN place counters**: should the decoder emit one Perfetto counter track per place per net? With many places this creates many tracks. | Optional, behind a `--pn-places` flag in the decoder tool. |
| OQ-4 | **Name registration from Machine/Net fields**: should `Machine` optionally carry `state_names: dict[int, str]`? Avoids separate `tracer.name_fsm_states()` call. | Convenience vs. clean separation; defer to implementation. |
| OQ-5 | **Compressed wire format**: for long captures, gzip the event array on export. | Add `flags` bit; trivial to add without breaking compatibility. |
