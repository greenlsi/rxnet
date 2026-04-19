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

## Platform ports

rxnet abstracts OS/RTOS primitives behind a thin header-only layer in
`include/rxnet/port/`.  The right port is selected automatically at compile
time; no source file changes are required when porting.

| Port | Selected when |
|------|--------------|
| `port/posix.h` | Default (Linux, macOS, any POSIX system) |
| `port/freertos.h` | `ESP_PLATFORM` detected, or `-DRXNET_PORT_FREERTOS` |
| `port/zephyr.h` | `CONFIG_ZEPHYR` detected, or `-DRXNET_PORT_ZEPHYR` |

To force a specific port: `-DRXNET_PORT_POSIX`, `-DRXNET_PORT_FREERTOS`, or
`-DRXNET_PORT_ZEPHYR`.

### Provided primitives

Each port defines these types and inline functions:

| Primitive | Purpose |
|-----------|---------|
| `rx_tick_t` | `int64_t` nanoseconds |
| `rx_tick_now()` | Monotonic clock read |
| `rx_tick_add_us(t, us)` | Add microseconds to a tick value |
| `rx_tick_compare(a, b)` | Compare two tick values |
| `rx_tick_sleep_until(t)` | Sleep until tick target |
| `rx_mutex_t` | Mutual exclusion lock |
| `rx_mutex_init/lock/unlock` | Mutex operations |
| `rx_thread_t` | Thread handle |
| `rx_thread_create(t, fn, arg)` | Spawn a thread |
| `rx_barrier_t` | Reusable generation barrier |
| `rx_barrier_init(b, n)` | Initialise barrier for n threads |
| `rx_barrier_wait(b)` | Wait until all n threads arrive |

The trace subsystem hooks (`RX_TRACE_NOW_NS`, `RX_TRACE_LOCK_*`) are also set
by each port, so including `rxnet/port.h` before `rxnet/trace.h` is sufficient.

### FreeRTOS / ESP-IDF

Build with the ESP-IDF build system.  Add `rxnet/c` as a CMake component:

**`components/rxnet/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS
        "../../rxnet/c/src/runtime.c"
        "../../rxnet/c/src/fsm.c"
        "../../rxnet/c/src/pn.c"
        "../../rxnet/c/src/cyclic.c"
        "../../rxnet/c/src/coop.c"
        "../../rxnet/c/src/thread.c"
    INCLUDE_DIRS
        "../../rxnet/c/include"
)
```

`ESP_PLATFORM` is set automatically by ESP-IDF, so `rxnet/port/freertos.h` is
selected without any extra flags.

**Tuning** (define in your component's `CMakeLists.txt` or `sdkconfig`):

| Macro | Default | Meaning |
|-------|---------|---------|
| `RXNET_FREERTOS_STACK_SIZE` | `4096` | Stack size per rxnet task (bytes) |
| `RXNET_FREERTOS_TASK_PRIORITY` | `5` | FreeRTOS task priority |
| `RXNET_FREERTOS_CORE_ID` | `-1` (any) | Pin tasks to a core (`0` or `1`) |

```cmake
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    RXNET_FREERTOS_STACK_SIZE=8192
    RXNET_FREERTOS_TASK_PRIORITY=10
    RXNET_FREERTOS_CORE_ID=1       # run all rxnet tasks on core 1
)
```

**Minimal `main.c` for ESP-IDF:**

```c
#include "rxnet/fsm.h"
#include "rxnet/thread.h"

static void app_main_task(void *arg)
{
    rx_fsm_runtime rt;
    rx_fsm_machine machine;
    /* ... init machines ... */

    rx_thread_exec te;
    rx_thread_exec_init(&te);
    rx_thread_exec_add(&te, &rt.runtime);
    rx_thread_exec_run(&te);  /* never returns */
}

void app_main(void)
{
    xTaskCreate(app_main_task, "rxnet", 8192, NULL, 5, NULL);
}
```

### Zephyr (nRF52 / nRF54)

Add `rxnet/c` as a Zephyr module.  Create a minimal `zephyr/module.yml` in the
rxnet repo root (or register it in your `west.yml` manifest):

**`zephyr/module.yml`**

```yaml
name: rxnet
build:
  cmake: c
  kconfig: c/Kconfig
```

**`c/Kconfig`**

```kconfig
config RXNET
    bool "rxnet reactive synchronous runtime"
    default y

config RXNET_STACK_SIZE
    int "Stack size for rxnet threads (bytes)"
    default 2048

config RXNET_THREAD_PRIORITY
    int "Zephyr thread priority for rxnet threads"
    default 5
```

**`c/CMakeLists.txt`** (Zephyr module variant)

```cmake
zephyr_library_named(rxnet)
zephyr_library_sources(
    src/runtime.c  src/fsm.c  src/pn.c
    src/cyclic.c   src/coop.c src/thread.c
)
zephyr_library_include_directories(include)
zephyr_library_compile_definitions(
    RXNET_PORT_ZEPHYR
    RXNET_ZEPHYR_STACK_SIZE=${CONFIG_RXNET_STACK_SIZE}
    RXNET_ZEPHYR_THREAD_PRIORITY=${CONFIG_RXNET_THREAD_PRIORITY}
)
```

**`prj.conf`** (add to your application):

```
CONFIG_RXNET=y
CONFIG_RXNET_STACK_SIZE=4096
CONFIG_RXNET_THREAD_PRIORITY=5
CONFIG_PTHREAD_IPC=n      # use native Zephyr primitives, not POSIX wrapper
```

**Stack pool sizing**  
Zephyr requires thread stacks declared at compile time.  The port pre-allocates
a pool of `RXNET_ZEPHYR_MAX_THREADS` stacks, where:

```
RXNET_ZEPHYR_MAX_THREADS = RXNET_MAX_RUNTIME_NODES × RXNET_THREAD_MAX_RUNTIMES
```

Both constants are set in `rxnet/config.h` (defaults: 8 nodes × 4 runtimes = 32
stacks).  Increase `RXNET_ZEPHYR_MAX_THREADS` directly if you need more:

```cmake
zephyr_library_compile_definitions(RXNET_ZEPHYR_MAX_THREADS=16)
```

**Minimal application thread:**

```c
#include "rxnet/fsm.h"
#include "rxnet/thread.h"
#include <zephyr/kernel.h>

K_THREAD_STACK_DEFINE(main_stack, 4096);
static struct k_thread main_thread;

static void rxnet_entry(void *a, void *b, void *c)
{
    rx_fsm_runtime rt;
    rx_fsm_machine machine;
    /* ... init machines ... */

    rx_thread_exec te;
    rx_thread_exec_init(&te);
    rx_thread_exec_add(&te, &rt.runtime);
    rx_thread_exec_run(&te);  /* never returns */
}

void main(void)
{
    k_thread_create(&main_thread, main_stack, K_THREAD_STACK_SIZEOF(main_stack),
                    rxnet_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}
```

## Specs

Design rationale and formal requirements live in `docs/specs/c/`.
