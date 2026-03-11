# rxnet (C)

`rxnet` is a small synchronous runtime for reactive models.

The C implementation has a shared internal runtime by phases:

- `include/rxnet/runtime.h`, `src/runtime.c`
- phase order per tick: latch inputs -> evaluate all nodes -> commit all nodes -> run deferred actions

Model frontends:

- FSM: `include/rxnet/fsm.h`, `src/fsm.c`
- Petri Net: `include/rxnet/pn.h`, `src/pn.c`

Both use typed application inputs via context buffers:

- `ctx->inputs`: mutable live inputs (owned by application)
- `ctx->latched_inputs`: immutable snapshot for the current tick
- `ctx->inputs_size`: bytes copied during latch

Runtime init requires external inputs:

- `rx_fsm_runtime_init(runtime, inputs_ptr, inputs_size, machine_capacity)`
- `rx_pn_runtime_init(runtime, inputs_ptr, inputs_size, net_capacity)`

## Build FSM example

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/runtime.c src/fsm.c examples/example.c -o fsm_example
./fsm_example
```

## ESP-IDF FSM example

- `examples/fsm/00-light/main.c`
- `examples/fsm/00-light/light_fsm.c`
- `examples/fsm/00-light/light_fsm.h`
- `examples/fsm/00-light/app_driver.c`
- `examples/fsm/00-light/app_driver.h`

This example shows periodic `rx_fsm_tick()` activation with `vTaskDelayUntil(...)` and ISR-driven input updates.

## Build Petri Net example

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/runtime.c src/pn.c examples/pn_example.c -o pn_example
./pn_example
```
