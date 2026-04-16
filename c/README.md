# rxnet (C)

`rxnet` is a small, allocation-free synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine.

## Tick phases (per cycle)

1. **Latch inputs** — each node snapshots its inputs atomically
2. **Evaluate** — compute next state / fire transitions (read-only on committed state)
3. **Commit** — publish new state; enqueue deferred actions
4. **Run deferred actions** — callbacks fire after all commits are visible
5. **Dump outputs** — each node writes its outputs to hardware or downstream

All nodes observe a consistent snapshot.  Guards in phase 2 never see a half-committed state.

## When to use FSM vs Petri Net

| | FSM | Petri Net |
|---|---|---|
| **Model** | State machine: one active state at a time | Token-flow graph: concurrent marking |
| **Transitions** | `from_state → to_state` guarded by a boolean | `consume arcs → produce arcs`, enabled by token counts |
| **Good for** | Linear state sequences, UI flows, mode control | Concurrency, resource sharing, event counting |
| **Actions** | One action per fired transition | One action per fired transition |
| **Example use** | Button toggles ON↔OFF | Blink with speed levels (OFF / X1 / X2) |

Use FSM when states are mutually exclusive.  
Use Petri Net when you need concurrency or event accumulation within a single node.

Both types are `rx_node` subtypes and can share a single `rx_runtime` tick (see `examples/mixed/`).

## Build

Standard C11.  No external dependencies.

```bash
make          # build library + all examples
make test     # run unit tests (71 tests)
make parity   # run C/Python cross-language parity check
make clean
```

Override compiler or flags:

```bash
CC=clang make
CFLAGS="-std=c11 -O2 -Iinclude" make
```

## Examples

### FSM examples

```bash
make light_cli && ./build/fsm_00_light   # on/off toggle
make auto_cli  && ./build/fsm_01_auto    # auto-off timer
make blink_cli && ./build/fsm_02_blink   # blink with speed control
make mix_cli   && ./build/fsm_03_mix     # all three FSMs together
```

### Petri Net examples

```bash
make pn_01_light && ./build/pn_01_light  # on/off toggle (PN)
make pn_02_auto  && ./build/pn_02_auto   # auto-off (PN)
make pn_03_blink && ./build/pn_03_blink  # blink with speed levels (PN)
make pn_04_mix   && ./build/pn_04_mix    # all three PNs + FSM CLI
```

### Mixed FSM + PN in one runtime

```bash
make mixed && ./build/mixed    # FSM light (button A) + PN light (button B), shared rx_runtime
```

## Basic integration pattern

```c
#include "rxnet/fsm.h"   /* or pn.h */

/* 1. Declare runtime and machine */
rx_fsm_runtime runtime;
rx_fsm_machine machine;

/* 2. Declare user data (inputs + state, owned by the node) */
int button = 0;

/* 3. Latch and dump callbacks */
static void latch(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    *(int *)user = read_gpio(BUTTON_PIN);
}
static void guard(const rx_fsm_context *ctx, void *user) {
    return *(int *)user;
}

/* 4. Transition table */
static const rx_fsm_transition transitions[] = {
    {STATE_OFF, STATE_ON,  guard, NULL},
    {STATE_ON,  STATE_OFF, guard, NULL},
};

/* 5. Init and run */
rx_fsm_runtime_init(&runtime, 1);
rx_fsm_machine_init(&machine, "light", STATE_OFF,
                    transitions, 2, &button, latch, /*dump=*/NULL);
rx_fsm_runtime_add_machine(&runtime, &machine);

while (1) {
    rx_fsm_tick(&runtime);
    /* sleep to next period */
}
```

## Concurrency patterns

All three patterns work without library changes:

### Cyclic executive (bare-metal)

```c
/* ISR: word-sized write is atomic on ARM Cortex-M */
void BUTTON_IRQHandler(void) { inputs.button = 1; }

while (1) {
    rx_fsm_tick(&runtime);   /* latch takes snapshot at start of tick */
    /* sleep to next period */
}
```

### OS threads (POSIX / Win32)

```c
/* Writer thread: */
pthread_mutex_lock(&lock);
inputs.button = 1;
pthread_mutex_unlock(&lock);

/* Tick thread: */
while (1) {
    pthread_mutex_lock(&lock);
    rx_fsm_tick(&runtime);
    pthread_mutex_unlock(&lock);
    nanosleep(&period, NULL);
}
```

### RTOS cooperative (FreeRTOS)

```c
/* ISR or driver task: */
void button_isr(void) {
    inputs.button = 1;                  /* atomic word write */
    xTaskNotifyGive(tick_task_handle);  /* wake tick task */
}

/* Tick task: */
void tick_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        rx_fsm_tick(&runtime);
    }
}
```

## Specs

Design rationale and formal requirements live in `docs/specs/c/`.
