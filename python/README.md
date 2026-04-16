# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine.

## Tick phases (per cycle)

1. **Latch inputs** — each node's `latch_inputs_cb` runs; nodes snapshot their own inputs
2. **Evaluate** — all nodes compute next state / fire transitions against the snapshot
3. **Commit** — all nodes apply their updates; deferred actions are enqueued
4. **Run deferred actions** — post-commit callbacks fire (timers, side-effects)
5. **Dump outputs** — each node's `dump_outputs_cb` runs (write outputs, clear flags)

All nodes observe a consistent snapshot.  Guards in phase 2 never see a half-committed peer.

Input ownership stays with each node's own data, passed via the `user` argument to callbacks.

## When to use FSM vs Petri Net

| | FSM | Petri Net |
|---|---|---|
| **Model** | One active state at a time | Token counts across places |
| **Transition** | `from_state → to_state` guarded by callable | `consume arcs → produce arcs` |
| **Best for** | Linear mode sequences, UI flows | Concurrency, resource sharing, event counting |
| **Example use** | Button toggles ON↔OFF | Blink with speed levels (OFF/X1/X2) |

Both `Machine` (FSM) and `Net` (PN) are `Node` subtypes and can share a single `Runtime`.

## Install

```bash
# From the python/ directory:
uv sync --extra dev
```

Requires Python 3.11+.

## Basic integration pattern (FSM)

```python
from rxnet.fsm import Machine, Transition, Runtime

OFF, ON = 0, 1
button = {"pressed": False}

def guard(ctx, user):
    return user["pressed"]

transitions = [
    Transition(from_state=OFF, to_state=ON,  guard=guard),
    Transition(from_state=ON,  to_state=OFF, guard=guard),
]

machine = Machine(
    name="light",
    initial_state=OFF,
    transitions=transitions,
    user=button,
    latch_inputs=lambda ctx, user: None,   # fill user["pressed"] here
    dump_outputs=lambda ctx, user: None,   # read machine.state here
)

runtime = Runtime(capacity=1)
runtime.add(machine)

while True:
    runtime.tick()
```

## Tests

```bash
cd python
uv run --extra dev pytest tests/ -v
```

56 tests covering runtime, FSM, and PN semantics.

## Examples

Interactive CLI examples mirror the C examples under `c/examples/`.  They share:

- `examples/app_driver.py` — simulated GPIO driver (host mock)
- `examples/cli.py` — non-blocking CLI helper using a background stdin thread

### FSM examples

| Directory | Description |
|---|---|
| `examples/fsm/00-basic/` | Basic non-interactive FSM demo |
| `examples/fsm/01-light/` | Simple toggle (button → ON/OFF) |
| `examples/fsm/02-auto/` | Auto-off timer; button resets countdown |
| `examples/fsm/03-blink/` | Three-speed blink; button cycles OFF→X1→X2→OFF |
| `examples/fsm/04-mix/` | One light + one blink + one auto in one runtime |

### Petri Net examples

| Directory | Description |
|---|---|
| `examples/pn/00-basic/` | Basic non-interactive PN demo |
| `examples/pn/01-light/` | Token-based toggle (REQUEST place) |
| `examples/pn/02-auto/` | Auto-off with signal place (P_AUTO_OFF_DUE) |
| `examples/pn/03-blink/` | Blink with signal place (P_TOGGLE_DUE) |
| `examples/pn/04-mix/` | One of each PN type in one runtime |

### Running

```bash
cd python
uv run examples/fsm/01-light/main.py
uv run examples/pn/01-light/main.py
# etc.
```

Common commands: `a`, `b`, `status`, `help`, `quit`

## Concurrency

Python's GIL means only one thread executes Python bytecode at a time.  rxnet makes no threading assumptions — choose the pattern that fits your host.

### Cyclic executive (single thread)

The simplest pattern: drive all ticks from one loop.

```python
import time

while True:
    inputs["button"] = read_button()
    runtime.tick()
    time.sleep(0.01)
```

### Threading (`threading.Thread`)

Protect both the input write and the tick with a single lock.

```python
import threading, time

lock = threading.Lock()
inputs = {"button": False}

def tick_thread():
    while True:
        with lock:
            runtime.tick()
        time.sleep(0.01)

def writer_thread():
    while True:
        with lock:
            inputs["button"] = read_button()
        time.sleep(0.001)

threading.Thread(target=tick_thread, daemon=True).start()
threading.Thread(target=writer_thread, daemon=True).start()
```

### Asyncio (tick as a coroutine)

Run `tick()` inside an asyncio task; update inputs from other coroutines — the event loop serialises them.

```python
import asyncio

async def tick_loop():
    while True:
        runtime.tick()
        await asyncio.sleep(0.01)

async def button_task():
    while True:
        await wait_for_button()
        inputs["button"] = True  # picked up on next tick_loop iteration

async def main():
    await asyncio.gather(tick_loop(), button_task())

asyncio.run(main())
```

## Specs

- [`docs/specs/python/requirements.md`](../docs/specs/python/requirements.md)
- [`docs/specs/python/design.md`](../docs/specs/python/design.md)
