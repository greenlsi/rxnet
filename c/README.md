# rxnet (C)

`rxnet` is a small synchronous runtime for reactive models.

The C implementation has a shared internal runtime by phases:

- `include/rxnet/runtime.h`, `src/runtime.c`
- phase order per tick: node latch inputs -> evaluate all nodes -> commit all nodes -> run deferred actions -> node dump outputs

Model frontends:

- FSM: `include/rxnet/fsm.h`, `src/fsm.c`
- Petri Net: `include/rxnet/pn.h`, `src/pn.c`

Inputs/outputs are model-owned and typically carried through each node `user` payload.
Each node can implement its own `latch_inputs` / `dump_outputs` callbacks through the base `rx_node_vtable`.

Runtime init requires only node capacity:

- `rx_fsm_runtime_init(runtime, machine_capacity)`
- `rx_pn_runtime_init(runtime, net_capacity)`

## Host/macOS CLI build (without ESP-IDF)

```bash
make -C c light_cli
./c/build/fsm_00_light
```

```bash
make -C c auto_cli
./c/build/fsm_01_auto
```

```bash
make -C c blink_cli
./c/build/fsm_02_blink
```

```bash
make -C c mix_cli
./c/build/fsm_03_mix
```

## Petri Net Async Examples

Examples live under `c/examples/pn/00-...`, `c/examples/pn/01-...`, etc.

```bash
make -C c pn_00_queue
./c/build/pn_00_queue
```

## ESP-IDF FSM example

- `examples/fsm/00-light/main.c`
- `examples/fsm/00-light/light_fsm.c`
- `examples/fsm/00-light/light_fsm.h`
- `examples/fsm/00-light/app_driver.c`
- `examples/fsm/00-light/app_driver.h`

This example shows periodic `rx_fsm_tick()` activation with `vTaskDelayUntil(...)` and ISR-driven input updates.
