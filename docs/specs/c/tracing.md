# Design Document: rxnet — C Tracing Subsystem

## Overview

This document specifies the optional tracing subsystem for the C implementation of `rxnet`.  
It covers event taxonomy, buffer design, API, drain protocol, binary wire format, and
visualization.

**Fundamental constraint**: when `RX_TRACE_ENABLE` is **not** defined at compile time, the
tracing subsystem generates **zero code and zero data** — macros expand to `((void)0)`, the
struct fields are absent, and the linker sees nothing to include. No runtime check, no branch,
no pointer.

---

## Goals

| Priority | Goal |
|---|---|
| **G1** | Zero code and zero data when `RX_TRACE_ENABLE` is not defined |
| **G2** | Machine-level trace: activations, transitions, worst-case response time |
| **G3** | User events: arbitrary `label_id` (uint16) + `value` (uint16) |
| **G4** | Phase-level trace: duration of latch / evaluate / commit / dump (optional) |
| **G5** | Drain buffer via a user-supplied write callback — transport-agnostic (UART, USB CDC, TCP, file) |
| **G6** | Common binary wire format with the Python implementation (same decoder tool) |
| **G7** | No heap allocation — buffer statically embedded in the runtime struct |

---

## Event Taxonomy

### Event types

```
RX_TRACE_TICK_START       slot N begins; which nodes are active this slot
RX_TRACE_NODE_START       node X begins latch phase (activation time)
RX_TRACE_NODE_END         node X finishes dump phase (completion time)
RX_TRACE_PHASE_START      optional: one of { latch, evaluate, commit, dump } begins
RX_TRACE_PHASE_END        optional: corresponding phase ends
RX_TRACE_FSM_TRANSITION   machine X: state A → state B
RX_TRACE_PN_FIRING        net X: transition T fires
RX_TRACE_USER             user-defined: label_id (uint16) + value (uint16)
```

All events carry a nanosecond-precision monotonic timestamp relative to `t0` (the first
call to `rx_trace_enable()`).

### Derived metrics (computed by the decoder, not stored)

- **Response time** of node X on tick T = `NODE_END.t_ns − NODE_START.t_ns`
- **WCRT** (worst-case response time) = `max(response_time)` over the trace window
- **Phase duration** = `PHASE_END.t_ns − PHASE_START.t_ns`
- **Activation jitter** = deviation of `NODE_START.t_ns` from the ideal period multiple

---

## Zero-Overhead Design

### Compile-time guard

All tracing code is guarded by `#ifdef RX_TRACE_ENABLE`. Public macros expand as follows:

```c
#ifdef RX_TRACE_ENABLE
  #define RX_TRACE_NODE_START(rt, node_id)   rx_trace_node_start((rt), (node_id))
  #define RX_TRACE_FSM(rt, id, from, to)     rx_trace_fsm_transition((rt), (id), (from), (to))
  #define RX_TRACE_USER(rt, label, value)    rx_trace_user_event((rt), (label), (value))
  /* ... */
#else
  #define RX_TRACE_NODE_START(rt, node_id)   ((void)0)
  #define RX_TRACE_FSM(rt, id, from, to)     ((void)0)
  #define RX_TRACE_USER(rt, label, value)    ((void)0)
  /* ... */
#endif
```

When `RX_TRACE_ENABLE` is not defined, the compiler sees `((void)0)` — the expression is
a no-op, produces no code, and generates no symbols. A link-time optimizer eliminates it
even from unoptimized builds.

### Buffer in the struct

```c
typedef struct rx_runtime {
    rx_node    **nodes;
    size_t       node_count;
    size_t       node_capacity;
    rx_context  *ctx;
    /* multi-rate scheduling fields ... */

#ifdef RX_TRACE_ENABLE
    rx_trace_buf_t trace;   /* zero additional size when not defined */
#endif
} rx_runtime_t;
```

No heap allocation. `rx_trace_buf_t` is a struct containing a fixed-size event array
(size controlled by `RX_TRACE_CAPACITY`, default 4096 events = 64 KB).

---

## Buffer Design

### `rx_trace_event_t` (16 bytes)

```c
typedef struct __attribute__((packed)) {
    uint64_t t_ns;      /* nanoseconds from t0                        */
    uint8_t  type;      /* RX_TRACE_* event type                      */
    uint8_t  node_id;   /* node index within the runtime (0–255)      */
    uint16_t a;         /* from_state / slot / phase_id / label_id    */
    uint16_t b;         /* to_state / user_value / 0                  */
    uint16_t c;         /* spare / dropped_count snapshot             */
} rx_trace_event_t;     /* 16 bytes                                   */
```

### `rx_trace_buf_t`

```c
#ifndef RX_TRACE_CAPACITY
#define RX_TRACE_CAPACITY  4096u   /* must be a power of 2 */
#endif

typedef struct {
    rx_trace_event_t  events[RX_TRACE_CAPACITY];
    uint32_t          head;     /* next write slot (mod CAPACITY) */
    uint32_t          count;    /* events stored (saturates at CAPACITY) */
    uint32_t          dropped;  /* events overwritten due to overflow */
    uint64_t          t0_ns;    /* monotonic ns at rx_trace_enable()  */
} rx_trace_buf_t;               /* 64 KB at default capacity          */
```

The ring is a **sliding window**: when `count == CAPACITY`, new events overwrite the oldest
and `dropped` increments. The decoder marks a gap wherever overflow is detected.

### Buffer sizing guide

| `RX_TRACE_CAPACITY` | RAM | Duration at 4 nodes / 100 Hz / all events |
|---|---|---|
| 1 024 | 16 KB | ~2.5 s |
| 4 096 | 64 KB | ~10 s |
| 16 384 | 256 KB | ~40 s |

Adjust `RX_TRACE_CAPACITY` via the build system:

```cmake
target_compile_definitions(my_target PRIVATE
    RX_TRACE_ENABLE
    RX_TRACE_CAPACITY=16384
)
```

---

## Clock Source

`rx_trace_now_ns()` is a thin abstraction over the platform clock:

```c
/* Default implementation (POSIX) */
static inline uint64_t rx_trace_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
```

For bare-metal targets, override the macro before including `rxnet/trace.h`:

```c
/* Cortex-M example using DWT cycle counter */
#define RX_TRACE_NOW_NS()  ((uint64_t)DWT->CYCCNT * 1000ULL / (SystemCoreClock / 1000000ULL))
#include "rxnet/trace.h"
```

The override must return a monotonically increasing `uint64_t` in nanoseconds (or
equivalent precision units, as long as the header stores the chosen unit consistently).

---

## API

### Enabling the tracer

```c
/* Enable tracing on a runtime and record t0. */
void rx_trace_enable(rx_runtime_t *rt);

/* Disable tracing (events stop recording; buffer is preserved for drain). */
void rx_trace_disable(rx_runtime_t *rt);

/* Clear the buffer. */
void rx_trace_reset(rx_runtime_t *rt);
```

### Public attachment API

The implementation exposes two complementary ways to attach tracing metadata:

```c
void rx_trace_attach(rx_trace_buf_t *buf, struct rx_node *node, uint8_t nid);
int  rx_trace_attach_runtime(rx_trace_buf_t *buf, struct rx_runtime *rt);
```

`rx_trace_attach()` is the low-level primitive: it binds one node to one trace
buffer and assigns an explicit `nid`.

`rx_trace_attach_runtime()` is the runtime-oriented helper:

- it walks every node currently registered in `rt`
- it is **idempotent** for nodes already attached to the same buffer
- it is **incremental** when the runtime topology grows
- it preserves the existing `nid` of previously known nodes
- it assigns fresh `nid` values only to newly discovered nodes

This is the preferred API when the application can add nodes, rebuild the
runtime, and then resynchronize tracing without renumbering the existing graph.

Example:

```c
rx_trace_buf_t tracer;
rx_trace_init(&tracer, 0);

rx_trace_attach_runtime(&tracer, &rt.runtime);

/* later: the runtime grows */
rx_fsm_runtime_add_machine(&rt, &aux_machine, 0, 0);
rx_runtime_build(&rt.runtime);

rx_trace_attach_runtime(&tracer, &rt.runtime);  /* attaches only aux_machine */
```

### Internal recording functions (called from runtime and model code via macros)

```c
void rx_trace_tick_start   (rx_runtime_t *rt, uint8_t slot, uint8_t active);
void rx_trace_node_start   (rx_runtime_t *rt, uint8_t node_id, uint8_t slot);
void rx_trace_node_end     (rx_runtime_t *rt, uint8_t node_id, uint8_t slot);
void rx_trace_phase_start  (rx_runtime_t *rt, uint8_t node_id, uint8_t phase);
void rx_trace_phase_end    (rx_runtime_t *rt, uint8_t node_id, uint8_t phase);
void rx_trace_fsm_transition(rx_runtime_t *rt, uint8_t node_id,
                              uint16_t from, uint16_t to, uint16_t trans_idx);
void rx_trace_pn_firing    (rx_runtime_t *rt, uint8_t node_id, uint16_t trans_idx);
void rx_trace_user_event   (rx_runtime_t *rt, uint16_t label_id, uint16_t value);
```

These are not called directly by application code. Use the macros.

### User events (public macro)

```c
/* Record a user event from application code. */
RX_TRACE_USER(rt, label_id, value);

/* Example: register label IDs as enum constants */
enum { LABEL_BTN_A = 0, LABEL_BTN_B = 1, LABEL_TIMEOUT = 2 };
RX_TRACE_USER(rt, LABEL_BTN_A, 1);
```

Label strings are registered separately in the name table (see binary format). The runtime
stores only the `uint16_t` ID per event.

### Phase tracing (opt-in at compile time)

Phase events are recorded only when `RX_TRACE_PHASES` is also defined:

```c
#define RX_TRACE_ENABLE
#define RX_TRACE_PHASES   /* adds ~4× event rate */
```

Without `RX_TRACE_PHASES`, `PHASE_START`/`PHASE_END` macros expand to `((void)0)`.

### Name table (link-time, not runtime)

Node names, state names, place names, and label strings are registered as static arrays
and embedded in the drain output by `rx_trace_drain()`. They are not stored per-event.

```c
/* Declare once at file scope (in a .c file) */
RX_TRACE_NODE_NAME(0, "light_a");
RX_TRACE_NODE_NAME(1, "blink_b");
RX_TRACE_NODE_NAME(2, "auto_c");

RX_TRACE_FSM_STATE(0, 0, "OFF");   /* node_id=0, state_id=0, name */
RX_TRACE_FSM_STATE(0, 1, "ON");

RX_TRACE_LABEL(LABEL_BTN_A, "button_a");
RX_TRACE_LABEL(LABEL_BTN_B, "button_b");
```

These macros expand to `const rx_trace_name_t` entries in a dedicated linker section
(`".rx_trace_names"`). `rx_trace_drain()` iterates the section to build the name table
in the wire format header. No runtime registration call needed.

---

## Drain Protocol

The drain is transport-agnostic. The application supplies a write callback:

```c
typedef void (*rx_trace_write_fn)(const uint8_t *data, size_t len, void *user);

/*
 * Serialize the ring buffer to the binary wire format and push it through
 * write_fn in chunks.  Acquires a consistent snapshot (disables interrupts
 * or uses a lock on the head pointer) to avoid torn reads.
 *
 * write_fn may be called multiple times (header, name table, event array).
 * write_fn must not call any rx_trace_* functions (reentrance not supported).
 */
void rx_trace_drain(rx_runtime_t *rt,
                    rx_trace_write_fn write_fn,
                    void *user);
```

### Example: drain over POSIX TCP socket

```c
static void write_to_fd(const uint8_t *data, size_t len, void *user) {
    int fd = *(int *)user;
    write(fd, data, len);   /* or send(), etc. */
}

/* In a server thread: */
int client_fd = accept(server_fd, NULL, NULL);
rx_trace_drain(&rt, write_to_fd, &client_fd);
close(client_fd);
```

### Example: drain over UART (bare-metal)

```c
static void write_to_uart(const uint8_t *data, size_t len, void *user) {
    (void)user;
    for (size_t i = 0; i < len; i++)
        uart_send_byte(data[i]);
}

rx_trace_drain(&rt, write_to_uart, NULL);
```

### Example: drain to file (POSIX host)

```c
FILE *f = fopen("trace.bin", "wb");
static void write_to_file(const uint8_t *data, size_t len, void *user) {
    fwrite(data, 1, len, (FILE *)user);
}
rx_trace_drain(&rt, write_to_file, f);
fclose(f);
```

The POSIX TCP example is the primary integration path for the development workflow: the
target opens a listening socket on a fixed port; the `rxnet-trace` decoder tool on the Mac
connects, receives the binary blob, and converts it to Perfetto JSON.

---

## Binary Wire Format

Shared with the Python implementation. The same `rxnet-trace` decoder tool on the Mac
handles both.

### Header (32 bytes fixed)

```
[0:4]    magic     = 0x52 0x58 0x4E 0x54  ("RXNT")
[4]      version   = 1
[5]      event_sz  = 16
[6]      lang      = 0x43  ('C' = C, 'P' = Python)
[7]      flags     bit 0: has_phases
                   bit 1: has_names
[8:16]   t0_ns     uint64 LE  (wall clock at first event)
[16:20]  n_events  uint32 LE
[20:24]  n_dropped uint32 LE
[24:32]  reserved  (zeros)
```

### Name table (variable, after header)

```
[1]  node_count
For each node:
  [1]  name_len
  [N]  name bytes (UTF-8)

[1]  fsm_table_count
For each FSM node:
  [1]  node_id
  [1]  state_count
  For each state:
    [1]  state_id
    [1]  name_len
    [N]  name bytes

[1]  label_count
For each label:
  [1]  label_id
  [1]  name_len
  [N]  name bytes
```

### Event array (n_events × 16 bytes)

```
Per event:
  [0:8]   t_ns    uint64 LE  nanoseconds from t0
  [8]     type    uint8
  [9]     node_id uint8
  [10:12] a       uint16 LE
  [12:14] b       uint16 LE
  [14:16] c       uint16 LE
```

Field semantics by type — identical to the Python specification:

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

## Integration Points in the Runtime

The `rx_runtime_tick()` function calls the macros at the natural boundaries:

```c
void rx_runtime_tick(rx_runtime_t *rt) {
    uint8_t slot   = rt->current_slot;
    uint8_t active = rt->slot_sizes[slot];

    RX_TRACE_TICK_START(rt, slot, active);

    /* Phase 1: latch inputs */
    rx_context_latch(rt->ctx);
    for (uint8_t i = 0; i < active; i++) {
        rx_node *n = rt->slots[slot][i];
        RX_TRACE_NODE_START(rt, i);
        RX_TRACE_PHASE_START(rt, i, RX_PHASE_LATCH);
        if (n->vtable->latch_inputs) n->vtable->latch_inputs(n, rt->ctx);
        RX_TRACE_PHASE_END(rt, i, RX_PHASE_LATCH);
    }
    /* ... evaluate, commit, dump analogously ... */

    for (uint8_t i = 0; i < active; i++) {
        rx_node *n = rt->slots[slot][i];
        RX_TRACE_PHASE_START(rt, i, RX_PHASE_DUMP);
        if (n->vtable->dump_outputs) n->vtable->dump_outputs(n, rt->ctx);
        RX_TRACE_PHASE_END(rt, i, RX_PHASE_DUMP);
        RX_TRACE_NODE_END(rt, i);
    }

    rt->current_slot = (rt->current_slot + 1) % rt->nslots;
}
```

**FSM commit hook** (in `rx_fsm.c`):

```c
void rx_fsm_commit(rx_node *node, rx_context *ctx) {
    rx_fsm_machine_t *m = (rx_fsm_machine_t *)node;
    int prev = m->state;
    /* ... apply next_state ... */
    if (m->state != prev) {
        RX_TRACE_FSM(m->rt, m->node_id, prev, m->state, m->fired_transition);
    }
}
```

The machine needs a back-pointer to `rt` and its `node_id` (set when registered via
`rx_fsm_runtime_add_machine()`). These two fields are added only when `RX_TRACE_ENABLE`
is defined:

```c
typedef struct rx_fsm_machine {
    rx_node            base;
    /* ... FSM fields ... */
#ifdef RX_TRACE_ENABLE
    rx_runtime_t      *rt;
    uint8_t            node_id;
#endif
} rx_fsm_machine_t;
```

---

## Development Workflow

```
Target (Linux / embedded)          Dev machine (Mac)
──────────────────────────         ──────────────────────────────
Build with:
  -DRX_TRACE_ENABLE
  -DRX_TRACE_CAPACITY=4096

rx_trace_enable(&rt);
/* ... run system ... */
listen on TCP :7777
  → drain when client connects     rxnet-trace http://target:7777 --out trace.json --open
  → reset buffer after drain            ↓
                                    Decode binary → Perfetto JSON
                                    Serve JSON locally on :random_port
                                    Open browser → ui.perfetto.dev?url=...
```

The target keeps the trace server running permanently. The developer can pull a snapshot
at any time without stopping the system. Multiple pulls accumulate (each pull is an
independent window from the sliding ring buffer).

---

## Diagram Generation

Both FSM and PN diagrams use **Graphviz DOT**. The drain includes a topology section that
the decoder uses to generate DOT source without any additional tool on the target.

| Model | DOT convention |
|---|---|
| FSM | `digraph`; states = `shape=circle`; initial state = invisible `point` node |
| PN | `digraph`; places = `shape=circle` with token count in label; transitions = `shape=rectangle` |

The `RX_FSM_T` macro and the X-macro state table supply all the information needed:
transition labels come from `RX_FSM_T`, state names from the X-macro name table.
No separate registration call is needed.

The HTML report rendered by the `rxnet-trace` decoder tool uses `@hpcc-js/wasm` (Graphviz
compiled to WebAssembly) for client-side DOT rendering — no Graphviz installation on the Mac.

---

## ISR Safety — Decision

**The ring buffer write is always ISR-safe.**

The critical section (16-byte event write + two counter increments) is protected by a
platform hook. On ARM Cortex-M this masks all interrupts for ~10–20 cycles; on POSIX it
uses an `atomic_flag` spinlock.

```c
#ifndef RX_TRACE_CRITICAL_ENTER
  #if defined(__arm__) || defined(__thumb__)
    #define RX_TRACE_CRITICAL_ENTER(s) do { (s) = __get_PRIMASK(); __disable_irq(); } while(0)
    #define RX_TRACE_CRITICAL_EXIT(s)  __set_PRIMASK(s)
  #else  /* POSIX — atomic_flag spinlock in trace.c */
    #define RX_TRACE_CRITICAL_ENTER(s) rx__trace_lock()
    #define RX_TRACE_CRITICAL_EXIT(s)  rx__trace_unlock()
  #endif
#endif
```

The platform hook is overridable before including `rxnet/trace.h`. The defaults cover the
two primary targets (ARM bare-metal and POSIX host).

---

## Multi-Runtime

Each runtime has its own `rx_trace_buf_t` and its own `t0_ns`. For multi-runtime systems:

**Now**: the decoder treats each binary blob as an independent Perfetto process (`pid`).
The user opens two windows or overlays them manually.

**Planned**: drift correction via `RX_TRACE_SYNC` events. Two sync events per runtime
(at session start and end) each recording a pair `(t_local, t_reference)`. The decoder
applies a two-point affine correction: `t_corrected = a × t_local + b`. Flag bit 2 in
the wire-format header is reserved for this.

---

## Open Questions

| # | Question | Impact |
|---|---|---|
| OQ-1 | **Bare-metal clock rollover**: `DWT->CYCCNT` rolls over every ~42 s at 100 MHz. Tracer should detect and stitch rollovers for long captures. | Only matters for long captures on fast MCUs. |
| OQ-2 | **Phase tracing cost**: 8 nodes × 4 phases × 100 Hz = 3200 extra events/sec; a 4096-event buffer fills in ~1.3 s. Document: disable `RX_TRACE_PHASES` for long production captures even with `RX_TRACE_ENABLE`. | Caveat in user guide. |
| OQ-3 | **`node_id` stability**: assigned by registration order. If order changes across builds, old trace files are mis-labeled. Consider a stable hash of the node name. | Long-term trace archival. |
