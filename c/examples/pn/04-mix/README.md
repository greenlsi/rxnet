# PN 03-mix

Three Petri Net nodes with an FSM CLI (one of each prior example):

- **A** (`light_pn`): toggle ON/OFF per button-A press
- **B** (`blink_pn`): OFF → X1 → X2 → OFF per button-A press
- **C** (`auto_pn`): ON with button-B, auto-OFF after timeout

The same scenario is implemented in **three execution models**:

| File | Executor | Threads | Periods |
|---|---|---|---|
| `main_cli.c` | `rx_cyclic_exec` — hyperperiod dispatch table | 1 | A,B 10 ms / C 20 ms |
| `main_coop.c` | `rx_coop_exec` — dynamic deadline scheduling | 1 | A,B 10 ms / C 20 ms |
| `main_threads.c` | `rx_thread_exec` — one thread per node, BSP barriers | 4 | A,B 10 ms / C 20 ms / CLI 10 ms |

PN nodes (A, B, C) and the FSM CLI cannot share a single `rx_runtime` (different
model types).  `main_cli.c` and `main_coop.c` use two runtimes (`pn_rt` + `cli_rt`).
`main_threads.c` passes both runtimes to a single `rx_thread_exec`; each runtime
forms an independent barrier group.

### Thread model

```
pn_rt  (group 0): light_a + blink_b + auto_c
  slot 0 (t = 0, 20, …): barrier(3)  — all three PN nodes
  slot 1 (t = 10, 30, …): barrier(2) — light_a + blink_b

cli_rt (group 1): cli
  → main thread (last node of last runtime)
```

Nodes within the same runtime synchronise via latch/commit barriers.
Runtimes run independently — no barriers cross runtime boundaries.

## Files

- `main_cli.c`: cyclic executive
- `main_coop.c`: cooperative multi-rate scheduler
- `main_threads.c`: parallel thread-per-node scheduler
- `light_pn.c/.h`, `blink_pn.c/.h`, `auto_pn.c/.h`: reused from `examples/pn/`
- `app_driver.c/.h`, `cli_fsm.c/.h`: reused from `examples/fsm/00-light/`

## Build

```bash
make -C c pn_03_mix          && ./c/build/pn_03_mix          # cyclic executive
make -C c pn_03_mix_coop     && ./c/build/pn_03_mix_coop     # cooperative
make -C c pn_03_mix_threads  && ./c/build/pn_03_mix_threads  # parallel threads
```

## CLI commands

- `a` / `press a` — trigger button A (affects A and B)
- `b` / `press b` — trigger button B (affects C)
- `status` — print state and output of A, B, C
- `freq <hz>` — set base blink frequency for B
- `timeout <ms>` — set auto-off timeout for C
- `sched` — report schedulability analysis using measured WCET samples
- `help`, `quit` / `exit`
