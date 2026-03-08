# rxnet (C)

`rxnet` is a small synchronous runtime for reactive models.

The C implementation provides two model types:

- FSM: `include/rxnet/fsm.h`, `src/fsm.c`
- Petri Net: `include/rxnet/pn.h`, `src/pn.c`

Both use typed application inputs via context buffers:

- `ctx->inputs`: mutable live inputs (written by app/drivers)
- `ctx->latched_inputs`: immutable snapshot for the current tick
- `ctx->inputs_size`: bytes copied during latch

Tick semantics:

1. app updates `ctx->inputs`
2. `tick` does `memcpy(latched_inputs, inputs, inputs_size)`
3. guards evaluate against `latched_inputs`
4. state/marking commit happens globally
5. deferred actions run after commit

## Build FSM example

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/fsm.c examples/example.c -o fsm_example
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
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/pn.c examples/pn_example.c -o pn_example
./pn_example
```
