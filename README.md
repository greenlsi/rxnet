# rxnet

`rxnet` is a small synchronous runtime for reactive models.  
It provides two model families — **FSM** and **Petri Net** — that share one phase-based execution engine.

Available in **C** (embedded-ready, allocation-free) and **Python** (for simulation and host tooling).

## How it works

Every tick runs the same five phases, in order, across all registered nodes:

1. **Latch inputs** — each node snapshots its inputs (safe to call from any driver)
2. **Evaluate** — compute next state / fire transitions against the snapshot
3. **Commit** — publish new state; enqueue deferred actions
4. **Run deferred actions** — side-effect callbacks fire after all commits
5. **Dump outputs** — each node writes its outputs to hardware or downstream

All nodes see a consistent snapshot.  No node can observe a half-committed peer.

## FSM vs Petri Net

| | FSM | Petri Net |
|---|---|---|
| **Model** | One active state at a time | Token counts across places |
| **Transition** | `from → to` guarded by boolean | `consume arcs → produce arcs` |
| **Best for** | Linear mode sequences | Concurrency, resource sharing, event counting |
| **Shared** | Same tick phases, same `rx_runtime` | |

Both `rx_fsm_machine` and `rx_pn_net` are `rx_node` subtypes and can share a single base `rx_runtime`.

## Getting started

### C

```bash
cd c
make              # build library + all examples
make test         # 71 unit tests
make parity       # cross-language parity check vs Python
```

Run an example:

```bash
./build/fsm_01_light    # button toggles light (FSM)
./build/pn_01_light     # button toggles light (PN)
./build/mixed           # FSM + PN sharing one rx_runtime
```

See [`c/README.md`](c/README.md) for build options, integration patterns, and concurrency guidance.

### Python

```bash
cd python
python examples/fsm/00-basic/main.py
python examples/pn/00-basic/main.py
```

Requires Python 3.11+.

## Examples

| Path | Model | Demonstrates |
|---|---|---|
| `c/examples/fsm/01-light/` | FSM | Simple on/off toggle |
| `c/examples/fsm/02-auto/` | FSM | Auto-off after timeout |
| `c/examples/fsm/03-blink/` | FSM | Blink with speed control |
| `c/examples/fsm/04-mix/` | FSM | Multiple FSMs in one runtime |
| `c/examples/pn/01-light/` | PN | Simple on/off toggle |
| `c/examples/pn/02-auto/` | PN | Auto-off after timeout |
| `c/examples/pn/03-blink/` | PN | Blink with speed levels (OFF/X1/X2) |
| `c/examples/pn/04-mix/` | FSM + PN | Multiple PNs + FSM CLI |
| `c/examples/mixed/` | FSM + PN | Shared base `rx_runtime` |

## Specs

Formal requirements and design documents:

- [`docs/specs/c/requirements.md`](docs/specs/c/requirements.md)
- [`docs/specs/c/design.md`](docs/specs/c/design.md)
- [`docs/specs/python/requirements.md`](docs/specs/python/requirements.md)
