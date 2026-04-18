# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine and a common set of executors.

## Tick phases (per cycle)

1. **Latch inputs** — global `ctx.latch_inputs()` snapshots shared inputs; each node's `latch_inputs()` runs
2. **Evaluate** — all nodes compute next state / fire transitions against the snapshot
3. **Commit** — all nodes apply updates; deferred actions are enqueued
4. **Dispatch deferred** — post-commit callbacks fire (timers, GPIO writes, side-effects)
5. **Dump outputs** — each node's `dump_outputs()` runs (write outputs, clear flags)

All nodes observe a consistent snapshot.  Guards in phase 2 never see a half-committed peer.

## When to use FSM vs Petri Net

| | FSM | Petri Net |
|---|---|---|
| **Model** | One active state at a time | Token counts across places |
| **Transition** | `from_state → to_state` guarded by callable | `consume arcs → produce arcs` |
| **Best for** | Linear mode sequences, UI flows | Concurrency, resource sharing, event counting |
| **Example use** | Button toggles ON↔OFF | Blink with speed levels (OFF/X1/X2) |

Both `Machine` (FSM) and `Net` (PN) implement the `Node` protocol and can share a single `Runtime`.

## Install

```bash
# From the python/ directory:
uv sync --extra dev
```

Requires Python 3.11+.

## Multi-rate scheduling

Each node registers its own `period_us` (microseconds).  The runtime
computes the hyperperiod dispatch table at build time:

```python
from rxnet.fsm import Machine, Runtime

rt = Runtime()
rt.add_machine(light_a,  10_000)   # 10 ms
rt.add_machine(blink_b,  10_000)   # 10 ms
rt.add_machine(auto_c,   20_000)   # 20 ms
rt.add_node(cli_node,    10_000)   # 10 ms — any node, not just Machine

# base  = GCD(10, 10, 20, 10) = 10 ms  →  rt.period_us
# hyper = LCM(10, 10, 20, 10) = 20 ms  →  2 slots
#
# slot 0 (t = 0, 20, …): all four nodes
# slot 1 (t = 10, 30, …): light_a + blink_b + cli_node
```

`period_us=0` (default) marks a node as *async*: it runs on every base
tick regardless of slot, preserving backward compatibility with code that
doesn't register periods.

## Executors

Three executors drive one or more runtimes at the correct intervals.

### `CyclicExecutive` — static hyperperiod dispatch

Calls `rt.tick()` at regular intervals based on `rt.period_us`.  Uses a
fixed slot table; simplest model, no concurrency.

```python
from rxnet import CyclicExecutive

ce = CyclicExecutive()
ce.add(rt)
ce.run()   # never returns
```

### `CoopExecutive` — cooperative deadline scheduler

Fires `rt.tick()` whenever the next deadline passes.  Overrun-tolerant:
deadline advances by one period regardless of actual execution time.
Supports multiple independent runtimes.

```python
from rxnet import CoopExecutive

ce = CoopExecutive()
ce.add(rt)
ce.run()   # never returns
```

### `ThreadExecutive` — BSP thread-per-node

Gives each node its own `threading.Thread`.  Three `threading.Barrier`
objects per hyperperiod slot enforce the reactive-synchronous guarantee
(latch → evaluate → commit → dump).

The **last node of the last runtime** runs in the calling (main) thread —
add a `CliNode` last to keep stdin access in the main thread.

```python
from rxnet import ThreadExecutive

te = ThreadExecutive()
te.add(rt)
te.run()   # never returns — last node runs in this thread
```

## Basic integration pattern (FSM)

```python
from rxnet import Machine, Runtime, Transition, CyclicExecutive

OFF, ON = 0, 1
button = {"pressed": False}

def guard(ctx, user):
    return user["pressed"]

machine = Machine(
    name="light",
    initial_state=OFF,
    transitions=[
        Transition(from_state=OFF, to_state=ON,  guard=guard),
        Transition(from_state=ON,  to_state=OFF, guard=guard),
    ],
    user=button,
    latch_inputs=lambda ctx, user: None,   # fill user["pressed"] here
    dump_outputs=lambda ctx, user: None,   # read machine.state here
)

rt = Runtime()
rt.add_machine(machine, 10_000)   # 10 ms

ce = CyclicExecutive()
ce.add(rt)
ce.run()
```

## Tests

```bash
cd python
uv run --extra dev pytest tests/ -v
```

Tests cover runtime, FSM, PN semantics, multi-rate scheduling, and BSP
barrier correctness.

## Examples

Interactive CLI examples mirror the C examples under `c/examples/`.  They share:

- `examples/app_driver.py` — simulated GPIO driver (host mock)
- `examples/cli.py` — non-blocking CLI helper using a background stdin thread

Each `04-mix` directory contains four executor variants:

| File | Model |
|---|---|
| `main.py` | `CyclicExecutive` — single thread, static dispatch |
| `main_coop.py` | `CoopExecutive` — single thread, deadline-based |
| `main_threads.py` | `ThreadExecutive` — BSP thread-per-node |
| `main_async.py` | asyncio (legacy) |

### FSM examples

| Directory | Description |
|---|---|
| `examples/fsm/00-basic/` | Basic non-interactive FSM demo |
| `examples/fsm/01-light/` | Simple toggle (button → ON/OFF) |
| `examples/fsm/02-auto/` | Auto-off timer; button resets countdown |
| `examples/fsm/03-blink/` | Three-speed blink; button cycles OFF→X1→X2→OFF |
| `examples/fsm/04-mix/` | Multi-rate: light + blink + auto in one runtime |

### Petri Net examples

| Directory | Description |
|---|---|
| `examples/pn/00-basic/` | Basic non-interactive PN demo |
| `examples/pn/01-light/` | Token-based toggle (REQUEST place) |
| `examples/pn/02-auto/` | Auto-off with signal place (P_AUTO_OFF_DUE) |
| `examples/pn/03-blink/` | Blink with signal place (P_TOGGLE_DUE) |
| `examples/pn/04-mix/` | Multi-rate: light + blink + auto in one runtime |

### Running

```bash
cd python
uv run examples/fsm/04-mix/main.py          # cyclic executive
uv run examples/fsm/04-mix/main_coop.py     # cooperative scheduler
uv run examples/fsm/04-mix/main_threads.py  # BSP threads
uv run examples/pn/04-mix/main.py           # same for Petri Nets
```

Common commands: `a`, `b`, `status`, `freq <hz>`, `timeout <ms>`, `help`, `quit`

## Specs

- [`docs/specs/python/requirements.md`](../docs/specs/python/requirements.md)
- [`docs/specs/python/design.md`](../docs/specs/python/design.md)
