# FSM 04-mix (Python)

Three FSM machines in a single multi-rate runtime (one of each prior
example):

- **A** (`light_fsm`): toggle ON/OFF per button-A press
- **B** (`blink_fsm`): OFF → BLINK_X1 → BLINK_X2 → OFF per button-A press
- **C** (`auto_fsm`): ON with button-B, auto-OFF after timeout

The same scenario is implemented in **four execution models**:

| File | Model | Threads | Periods |
|---|---|---|---|
| `main.py` | Cyclic executive + hyperperiod table | 1 | A+B 10 ms / C 20 ms |
| `main_coop.py` | Cooperative deadline scheduler | 1 | A+B 10 ms / C 20 ms |
| `main_threads.py` | BSP thread-per-node | 4 | A+B 10 ms / C 20 ms |
| `main_async.py` | asyncio cooperative (legacy) | 1 | A+B 10 ms / C 20 ms |

### Single-runtime multi-rate design

All four nodes (light_a, blink_b, auto_c, cli_node) live in **one**
`Runtime`.  Each machine registers its own `period_us`:

```
rt.add_machine(light_a,  10_000)   # 10 ms
rt.add_machine(blink_b,  10_000)   # 10 ms
rt.add_machine(auto_c,   20_000)   # 20 ms
rt.add_node(cli_node,    10_000)   # 10 ms
```

The runtime computes the hyperperiod table at build time:

```
base  = GCD(10, 10, 20, 10) = 10 ms  →  rt.period_us
hyper = LCM(10, 10, 20, 10) = 20 ms  →  2 slots

slot 0 (t = 0, 20, 40, …): light_a + blink_b + auto_c + cli
slot 1 (t = 10, 30, 50, …): light_a + blink_b + cli
```

A and B share `BUTTON_A_GPIO`, so they tick together in every slot —
they see the same latched button event in the same latch phase.  C uses
`BUTTON_B_GPIO` and runs only in slot 0.

### Execution model notes

**`main.py` (cyclic executive)**: `CyclicExecutive` drives the single
runtime with a static slot table.  `rt.tick()` is called every 10 ms.
Nodes run sequentially.  Simplest model — no concurrency.

**`main_coop.py` (cooperative)**: `CoopExecutive` drives the runtime
dynamically: it fires `rt.tick()` whenever the next deadline has passed.
Overrun-tolerant — deadline advances by one period regardless of actual
execution time.

**`main_threads.py` (BSP threads)**: `ThreadExecutive` gives each node
its own `threading.Thread`.  Three `threading.Barrier` objects per slot
enforce the reactive-synchronous guarantee (latch → evaluate → commit →
dump).  The **cli node is added last** and runs in the main thread to
keep stdin access.

**`main_async.py` (asyncio — legacy)**: two asyncio Tasks drive separate
runtimes at different rates.  Kept for reference; the three models above
are preferred.

## Files

- `main.py`: cyclic executive (single runtime, static dispatch)
- `main_coop.py`: cooperative deadline scheduler
- `main_threads.py`: BSP thread-per-node executor
- `main_async.py`: asyncio cooperative (legacy, multi-runtime)
- FSM modules reused from `examples/fsm/01-light/`, `02-auto/`, `03-blink/`
- `examples/app_driver.py`, `examples/cli.py`: shared host mock and CLI

## Run

```bash
cd python

uv run examples/fsm/04-mix/main.py           # cyclic executive

uv run examples/fsm/04-mix/main_coop.py      # cooperative deadline scheduler

uv run examples/fsm/04-mix/main_threads.py   # BSP thread-per-node

uv run examples/fsm/04-mix/main_async.py     # asyncio (legacy)
```

## CLI commands

- `a` / `press a` — trigger button A (affects A and B)
- `b` / `press b` — trigger button B (affects C)
- `status` — print state of A, B, C
- `freq <hz>` — set base blink frequency for B
- `timeout <ms>` — set auto-off timeout for C
- `help` — list commands
- `quit` / `exit`
