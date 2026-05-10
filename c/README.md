# rxnet (C)

`rxnet` is a small, allocation-free synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine.

## Tick phases (per cycle)

1. **Latch** — each active node snapshots its inputs
2. **Evaluate** — compute next state / fire transitions and enqueue deferred actions
3. **Commit** — publish new state and dispatch deferred actions after all active commits
4. **Dump** — each active node writes its outputs to hardware or downstream

Each activation group has two barriers: after `evaluate`, and after `commit`
plus deferred-action dispatch.  All nodes in the group observe a consistent
snapshot.  Guards in phase 2 never see a half-committed state, and dumps only
run after every active node has committed.

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

Periods are registered **per node**, not per runtime.  The runtime stores the
scheduling parameters; each executor owns the scheduling structure it needs:

```c
rx_fsm_runtime_add_machine(&rt, &light_a, 10000, 0);  /* 10 ms */
rx_fsm_runtime_add_machine(&rt, &blink_b, 10000, 0);  /* 10 ms */
rx_fsm_runtime_add_machine(&rt, &auto_c,  20000, 0);  /* 20 ms */
/* base = GCD(10, 10, 20) = 10 ms → 2 slots
   slot 0: light_a + blink_b + auto_c
   slot 1: light_a + blink_b              */
```

The cyclic executive materialises this hyperperiod table.  The cooperative and
threaded executors do not: they track the next activation instant for each
periodic node and order ready work by effective deadline.

Nodes with `period_us = 0` are async: executors include them when they service
their runtime, but the node advances only when its own guards fire.

## Executors

Three executors are provided.  They read per-node scheduling parameters from the
runtime and handle timing internally.

### `rx_cyclic_exec` — cyclic executive

Static hyperperiod dispatch table.  Single thread, deterministic slot order.
Suitable for bare-metal and simple RTOS configurations.

```c
rx_cyclic_exec ce;
rx_cyclic_exec_init(&ce);
rx_cyclic_exec_add(&ce, &runtime.runtime);
rx_cyclic_exec_run(&ce); /* returns after rx_cyclic_exec_stop(&ce) */
```

### `rx_coop_exec` — cooperative multi-rate

Dynamic deadline scheduling: runs the nodes whose next activation is due, in
deadline order, then sleeps until the nearest next activation.  Single thread —
no mutexes needed, suitable for cooperative RTOS patterns.

```c
rx_coop_exec ce;
rx_coop_exec_init(&ce);
rx_coop_exec_add(&ce, &runtime.runtime);
rx_coop_exec_run(&ce); /* returns after rx_coop_exec_stop(&ce) */
```

Multiple runtimes can be registered; the scheduler picks the earliest deadline across all of them.

### `rx_thread_exec` — parallel thread-per-node (BSP barriers)

One thread per periodic node.  Activation groups are created inside the executor
for absolute activation instants, so no hyperperiod table is stored.  Two
barriers per live activation group enforce the reactive-synchronous guarantee
with true parallelism:

- **eval_b**: all active nodes have latched and evaluated.
- **commit_b**: all active nodes have committed and dispatched deferred actions.

Each node gets its own `rx_context` (no shared deferred queue).
The last node of the last runtime runs in the calling (main) thread.  Async
nodes (`period_us = 0`) are not accepted by `rx_thread_exec`: a single async
thread cannot safely participate in multiple overlapping activation groups.

```c
rx_thread_exec te;
rx_thread_exec_init(&te);
rx_thread_exec_add(&te, &runtime.runtime);  /* one runtime: all nodes get threads */
rx_thread_exec_run(&te); /* returns after rx_thread_exec_stop(&te) */
```

Each executor also has an `rx_*_exec_on_stop()` hook for application/model
shutdown logic that must run once before `rx_*_exec_run()` returns.

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
| `rx_thread_configure_fifo(t, rank, ranks)` | Try fixed-priority FIFO scheduling |
| `rx_thread_configure_current_fifo(rank, ranks)` | Try FIFO for the calling thread |
| `rx_barrier_t` | Reusable generation barrier |
| `rx_barrier_init(b, n)` | Initialise barrier for n threads |
| `rx_barrier_reset(b, n)` | Reuse a barrier for a new participant count |
| `rx_barrier_wait(b)` | Wait until all n threads arrive |

The trace subsystem hooks (`RX_TRACE_NOW_NS`, `RX_TRACE_LOCK_*`) are also set
by each port, so including `rxnet/port.h` before `rxnet/trace.h` is sufficient.

### POSIX / CMake (Linux, macOS, desktop)

Build and install:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

Consume in a downstream CMake project:

```cmake
find_package(rxnet REQUIRED)
target_link_libraries(myapp PRIVATE rxnet::rxnet)
```

Or without installing, build locally and add the sources directly (see
`c/CMakeLists.txt` for the source list).

### ESP-IDF (ESP32 / ESP32-S3 / ESP32-C3)

rxnet ships a ready-made ESP-IDF component.  The root `CMakeLists.txt`
contains an `idf_component_register()` call; `idf_component.yml` provides
the component manifest for the IDF Component Manager.

**Option A — local path** (sibling repo or git submodule):

```cmake
# In your project's CMakeLists.txt, before idf_build_process / project():
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../rxnet)
```

**Option B — IDF Component Manager** (`idf_component.yml` in your app):

```yaml
dependencies:
  rxnet:
    path: ../rxnet   # local path, or a registry URL once published
```

`ESP_PLATFORM` is set automatically by ESP-IDF, so `rxnet/port/freertos.h`
is selected without any extra flags.  No `port/freertos.c` is needed — the
FreeRTOS port is header-only.

**Tuning** (in your app's `CMakeLists.txt`):

| Macro | Default | Meaning |
|-------|---------|---------|
| `RXNET_FREERTOS_STACK_SIZE` | `4096` | Task stack depth (words) |
| `RXNET_FREERTOS_TASK_PRIORITY` | `5` | FreeRTOS task priority |
| `RXNET_FREERTOS_CORE_ID` | `-1` (any) | Core affinity (`0` or `1` to pin) |

```cmake
idf_component_get_property(rxnet_lib rxnet COMPONENT_LIB)
target_compile_definitions(${rxnet_lib} PUBLIC
    RXNET_FREERTOS_STACK_SIZE=8192
    RXNET_FREERTOS_CORE_ID=1
)
```

**Minimal `main.c`:**

```c
#include "rxnet/fsm.h"
#include "rxnet/coop.h"

static rx_fsm_runtime  rt;
static rx_fsm_machine  machine;
static rx_coop_exec    ce;

void app_main(void)
{
    rx_fsm_runtime_init(&rt, 1);
    /* ... machine init ... */
    rx_fsm_runtime_add_machine(&rt, &machine, 10000, 0); /* 10 ms */

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &rt.runtime);
    rx_coop_exec_run(&ce);  /* returns after rx_coop_exec_stop(&ce) */
}
```

### Zephyr / nRF Connect SDK (nRF52, nRF54)

rxnet ships a Zephyr module.  `module.yml` at the repo root declares it;
`zephyr/CMakeLists.txt` registers the library using Zephyr's native macros.
`CONFIG_ZEPHYR` is set automatically by Zephyr, so `rxnet/port/zephyr.h`
is selected without any extra flags.

**Option A — `ZEPHYR_EXTRA_MODULES`** (sibling repo, no west manifest needed):

```cmake
# In your app's CMakeLists.txt, before find_package(Zephyr):
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../rxnet)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

No `target_link_libraries` needed — `zephyr_library_named()` wires the
library and its headers into the Zephyr build automatically.

**Option B — `west.yml` manifest**:

```yaml
manifest:
  projects:
    - name: rxnet
      path: rxnet
      url: https://github.com/greenlsi/rxnet
      revision: main
```

**Required `prj.conf` option:**

```
CONFIG_EVENTS=y   # enables k_condvar, used by rx_barrier_wait
```

**Tuning** (override via `-D` or `prj.conf`):

| Macro | Default | Meaning |
|-------|---------|---------|
| `RXNET_ZEPHYR_STACK_SIZE` | `2048` | Stack per rxnet thread (bytes) |
| `RXNET_ZEPHYR_THREAD_PRIORITY` | `5` | Zephyr thread priority |
| `RXNET_ZEPHYR_MAX_THREADS` | `RXNET_MAX_RUNTIME_NODES × RXNET_THREAD_MAX_RUNTIMES` | Pre-allocated stack pool size |

```cmake
# In your app's CMakeLists.txt (after find_package(Zephyr)):
target_compile_definitions(app PRIVATE
    RXNET_ZEPHYR_STACK_SIZE=4096
    RXNET_ZEPHYR_THREAD_PRIORITY=3
)
```

**Stack pool sizing**  
Zephyr requires thread stacks to be declared at compile time.  The port
pre-allocates a pool of `RXNET_ZEPHYR_MAX_THREADS` stacks in
`src/port/zephyr.c`.  Defaults from `config.h`: 16 nodes × 8 runtimes = 128
slots.  Override if your application needs more:

```cmake
target_compile_definitions(app PRIVATE RXNET_ZEPHYR_MAX_THREADS=16)
```

**Minimal `main.c`:**

```c
#include "rxnet/fsm.h"
#include "rxnet/coop.h"

static rx_fsm_runtime  rt;
static rx_fsm_machine  machine;
static rx_coop_exec    ce;

int main(void)
{
    rx_fsm_runtime_init(&rt, 1);
    /* ... machine init ... */
    rx_fsm_runtime_add_machine(&rt, &machine, 10000, 0); /* 10 ms */

    rx_coop_exec_init(&ce);
    rx_coop_exec_add(&ce, &rt.runtime);
    rx_coop_exec_run(&ce);  /* returns after rx_coop_exec_stop(&ce) */

    return 0;
}
```

## Specs

Design rationale and formal requirements live in `docs/specs/c/`.
