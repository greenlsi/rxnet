# rxnet (C)

`rxnet` is a small, allocation-free synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine.

## Tick phases (per cycle)

1. **Latch inputs** — each node snapshots its inputs
2. **Evaluate** — compute next state / fire transitions (read-only on latched snapshot)
3. **Commit** — publish new state; enqueue deferred actions
4. **Run deferred actions** — callbacks fire after all commits are visible
5. **Dump outputs** — each node writes its outputs to hardware or downstream

All nodes in a tick observe a consistent snapshot.  Guards in phase 2 never see a half-committed state.

## When to use FSM vs Petri Net

| | FSM | Petri Net |
|---|---|---|
| **Model** | State machine: one active state at a time | Token-flow graph: concurrent marking |
| **Transitions** | `from_state → to_state` guarded by a boolean | `consume arcs → produce arcs`, enabled by token counts |
| **Good for** | Linear state sequences, UI flows, mode control | Concurrency, resource sharing, event counting |
| **Example use** | Button toggles ON↔OFF | Blink with speed levels (OFF / X1 / X2) |

Use FSM when states are mutually exclusive.  
Use Petri Net when you need concurrency or event accumulation within a single node.

Both types are `rx_node` subtypes and can share a single `rx_runtime` (see `examples/mixed/`).

## Build

Standard C99.  No external dependencies (threads require `-lpthread`).

```bash
make          # build library + all examples
make test     # run unit tests
make clean
```

Override compiler or flags:

```bash
CC=clang make
CFLAGS="-std=c99 -O2 -Iinclude" make
```

## Multi-rate scheduling

Periods are registered **per node**, not per runtime.  The runtime builds a
hyperperiod dispatch table automatically:

```c
rx_fsm_runtime_add_machine(&rt, &light_a, 10000, 0);  /* 10 ms */
rx_fsm_runtime_add_machine(&rt, &blink_b, 10000, 0);  /* 10 ms */
rx_fsm_runtime_add_machine(&rt, &auto_c,  20000, 0);  /* 20 ms */
/* base = GCD(10, 10, 20) = 10 ms → 2 slots
   slot 0: light_a + blink_b + auto_c
   slot 1: light_a + blink_b              */
```

Nodes with `period_us = 0` are async: they run every base tick but advance only when their own guards fire.

## Executors

Three executors are provided.  All read `rt->period_us` (set by the runtime
build step) and handle timing internally.

### `rx_cyclic_exec` — cyclic executive

Static hyperperiod dispatch table.  Single thread, deterministic slot order.
Suitable for bare-metal and simple RTOS configurations.

```c
rx_cyclic_exec ce;
rx_cyclic_exec_init(&ce);
rx_cyclic_exec_add(&ce, &runtime.runtime);
rx_cyclic_exec_run(&ce); /* never returns */
```

### `rx_coop_exec` — cooperative multi-rate

Dynamic deadline scheduling: runs whichever runtime is due, then sleeps until
the nearest next deadline.  Single thread — no mutexes needed, suitable for
cooperative RTOS patterns.

```c
rx_coop_exec ce;
rx_coop_exec_init(&ce);
rx_coop_exec_add(&ce, &runtime.runtime);
rx_coop_exec_run(&ce); /* never returns */
```

Multiple runtimes can be registered; the scheduler picks the earliest deadline across all of them.

### `rx_thread_exec` — parallel thread-per-node (BSP barriers)

One pthread per node.  Two barriers per hyperperiod slot enforce the
reactive-synchronous guarantee with true parallelism:

- **latch_b[s]**: all nodes active in slot s arrive → latch inputs in parallel → evaluate in parallel.
- **commit_b[s]**: all evaluations done → commit outputs in parallel.

Each node gets its own `rx_context` (no shared deferred queue).
The last node of the last runtime runs in the calling (main) thread.

```c
rx_thread_exec te;
rx_thread_exec_init(&te);
rx_thread_exec_add(&te, &runtime.runtime);  /* one runtime: all nodes get threads */
rx_thread_exec_run(&te); /* never returns */
```

Multiple runtimes can be registered; each forms an independent barrier group:

```c
rx_thread_exec_add(&te, &pn_rt.runtime);   /* PN nodes → parallel threads */
rx_thread_exec_add(&te, &cli_rt.runtime);  /* FSM cli → main thread (last) */
```

## Examples

### FSM examples

```bash
make fsm_01_light       && ./build/fsm_01_light        # on/off toggle
make fsm_02_auto        && ./build/fsm_02_auto         # auto-off timer
make fsm_03_blink       && ./build/fsm_03_blink        # blink with speed control
make fsm_04_mix_cyclic  && ./build/fsm_04_mix_cyclic   # cyclic executive
make fsm_04_mix_coop    && ./build/fsm_04_mix_coop     # cooperative scheduler
make fsm_04_mix_threads && ./build/fsm_04_mix_threads  # parallel threads
```

### Petri Net examples

```bash
make pn_01_light       && ./build/pn_01_light          # on/off toggle (PN)
make pn_02_auto        && ./build/pn_02_auto           # auto-off (PN)
make pn_03_blink       && ./build/pn_03_blink          # blink with speed levels (PN)
make pn_04_mix_cyclic  && ./build/pn_04_mix_cyclic     # cyclic executive
make pn_04_mix_coop    && ./build/pn_04_mix_coop       # cooperative scheduler
make pn_04_mix_threads && ./build/pn_04_mix_threads    # parallel threads
```

### Mixed FSM + PN in one runtime

```bash
make mixed && ./build/mixed    # FSM light (button A) + PN light (button B)
```

## Basic integration pattern

```c
#include "rxnet/fsm.h"

rx_fsm_runtime runtime;
rx_fsm_machine machine;
int button = 0;

static void latch(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    *(int *)user = read_gpio(BUTTON_PIN);
}
static int guard(const rx_fsm_context *ctx, void *user) {
    (void)ctx;
    return *(int *)user;
}

static const rx_fsm_transition transitions[] = {
    {STATE_OFF, STATE_ON,  guard, NULL},
    {STATE_ON,  STATE_OFF, guard, NULL},
};

rx_fsm_runtime_init(&runtime, 1);
rx_fsm_machine_init(&machine, "light", STATE_OFF,
                    transitions, 2, &button, latch, /*dump=*/NULL);
rx_fsm_runtime_add_machine(&runtime, &machine, 10000, 0); /* 10 ms */

rx_cyclic_exec ce;
rx_cyclic_exec_init(&ce);
rx_cyclic_exec_add(&ce, &runtime.runtime);
rx_cyclic_exec_run(&ce);
```

## Specs

Design rationale and formal requirements live in `docs/specs/c/`.
