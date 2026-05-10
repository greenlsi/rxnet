# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.  
It supports two model families — **FSM** and **Petri Net** — that share one phase-based execution engine and a common set of executors.

## Tick phases (per cycle)

1. **Latch** — global `ctx.latch_inputs()` snapshots shared inputs; each node's `latch_inputs()` runs
2. **Evaluate** — all nodes compute next state / fire transitions against the snapshot
3. **Commit** — all nodes apply updates; deferred actions are enqueued
4. **Dump** — each node's `dump_outputs()` runs after deferred action dispatch

Deferred actions dispatch after commit and before dump, but they are not a
separate node phase.

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

Each node registers its own `period_us` and optional `deadline_us`
(microseconds).  The runtime validates the metadata and computes only the base
period; executors own the activation table or next-activation state:

```python
from rxnet.fsm import Machine, Runtime

rt = Runtime()
rt.add_machine(light_a,  10_000)   # 10 ms
rt.add_machine(blink_b,  10_000)   # 10 ms
rt.add_machine(auto_c,   20_000)   # 20 ms
rt.add_node(cli_node,    10_000)   # 10 ms — any node, not just Machine

# base = GCD(10, 10, 20, 10) = 10 ms  →  rt.period_us
# rt.nslots == 0: the runtime no longer materializes activation slots
```

`period_us=0` (default) marks a node as *async*. Manual `rt.tick()` runs all
nodes. Multi-rate executors include async nodes when they service a runtime;
`ThreadExecutive` rejects async nodes because one async thread cannot safely
join multiple overlapping activation groups.

## Executors

Three executors drive one or more runtimes at the correct intervals.

### `CyclicExecutive` — static hyperperiod dispatch

Builds a fixed hyperperiod table inside the executive and calls explicit node
groups at each base tick; simplest model, no concurrency.

```python
from rxnet import CyclicExecutive

ce = CyclicExecutive()
ce.add(rt)
ce.run()   # returns when ce.stop() is requested
```

### `CoopExecutive` — cooperative deadline scheduler

Runs the due nodes whose next activation has passed, ordered by effective
deadline.  Overrun-tolerant: each activation advances by one period regardless
of actual execution time.
Supports multiple independent runtimes.

```python
from rxnet import CoopExecutive

ce = CoopExecutive()
ce.add(rt)
ce.run()   # returns when ce.stop() is requested
```

### `ThreadExecutive` — BSP thread-per-node

Gives each periodic node its own `threading.Thread`.  Per-activation groups use
two barriers: after evaluate, and after commit plus deferred action dispatch.
All nodes in one activation group see the same `ctx.activation_us`.

The **last node of the last runtime** runs in the calling (main) thread —
add a `CliNode` last to keep stdin access in the main thread.

```python
from rxnet import ThreadExecutive

te = ThreadExecutive()
te.add(rt)
te.run()   # returns when te.stop() is requested
```

All executors accept an optional `on_stop` callback, or one can be
registered later with `.on_stop(callback)`, to run application/model
shutdown logic once before `run()` returns.

Executors can enable automatic schedulability checks with
`.enable_sched_check(True)` and report calculations with `.check_schedulability()`.
`CyclicExecutive` checks the hyperperiod table with measured WCETs;
`CoopExecutive` uses response-time analysis with cooperative blocking;
`ThreadExecutive` reports the analysis as unsupported because Python does not
provide fixed-priority FIFO thread scheduling.

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
