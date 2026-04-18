# FSM 03-mix

Three machines in one runtime (one of each prior example):

- **A** (`light_fsm`): toggle ON/OFF per button-A press
- **B** (`blink_fsm`): OFF → X1 → X2 → OFF per button-A press
- **C** (`auto_fsm`): ON with button-B, auto-OFF after timeout

The same scenario is implemented in **three execution models**:

| File | Executor | Threads | Periods |
|---|---|---|---|
| `main_cli.c` | `rx_cyclic_exec` — hyperperiod dispatch table | 1 | A,B 10 ms / C 20 ms |
| `main_coop.c` | `rx_coop_exec` — dynamic deadline scheduling | 1 | A,B 10 ms / C 20 ms |
| `main_threads.c` | `rx_thread_exec` — one thread per node, BSP barriers | 4 | A,B 10 ms / C 20 ms / CLI 10 ms |

All three use **one runtime** with periods registered per machine.
The runtime builds the hyperperiod table (base = GCD = 10 ms, 2 slots) automatically.

### Thread model

`rx_thread_exec` gives each node its own pthread and its own `rx_context`
(no shared deferred queue).  Two barriers per hyperperiod slot enforce the
reactive-synchronous guarantee:

- **latch_b[s]**: all nodes active in slot s arrive → latch inputs in parallel → evaluate in parallel.
- **commit_b[s]**: all evaluations done → commit outputs in parallel.

The CLI node is added last and runs in the main thread (stdin access).

```
slot 0 (t = 0, 20, 40, …): light_a + blink_b + auto_c + cli  → barrier(4)
slot 1 (t = 10, 30, 50, …): light_a + blink_b + cli           → barrier(3)
```

Because all nodes share a barrier before evaluating, light_a and blink_b are
guaranteed to see the same BUTTON_A_GPIO snapshot even though they run in
separate threads.

## Files

- `main_cli.c`: cyclic executive
- `main_coop.c`: cooperative multi-rate scheduler
- `main_threads.c`: parallel thread-per-node scheduler
- `light_fsm.c/.h`, `blink_fsm.c/.h`, `auto_fsm.c/.h`, `cli_fsm.c/.h`
- `app_driver.c/.h`: GPIO mock (host) / ISR driver (ESP-IDF)

## Build

```bash
make -C c mix_cli        && ./c/build/fsm_03_mix          # cyclic executive
make -C c mix_coop       && ./c/build/fsm_03_mix_coop     # cooperative
make -C c mix_threads    && ./c/build/fsm_03_mix_threads  # parallel threads
```

## CLI commands

- `a` / `press a` — trigger button A (affects A and B)
- `b` / `press b` — trigger button B (affects C)
- `status` — print state and output of A, B, C
- `freq <hz>` — set base blink frequency for B
- `timeout <ms>` — set auto-off timeout for C
- `help`, `quit` / `exit`
