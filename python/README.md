# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.

The Python package has a shared internal runtime with a 5-phase tick:

1. **Global latch** — snapshot shared inputs (`context.latched_inputs = copy.copy(context.inputs)`)
2. **Per-node latch** — each node's `latch_inputs_cb` runs (read GPIO, set signal places, etc.)
3. **Evaluate** — all nodes compute their next state / fire flags
4. **Commit** — all nodes apply their updates; deferred actions are enqueued
5. **Deferred actions** — post-commit callbacks run (timer start, output side-effects)
6. **Dump outputs** — each node's `dump_outputs_cb` runs (write GPIO, print state, etc.)

Model frontends:

- FSM: `rxnet.fsm` — `Machine` + `Transition`, first-match semantics
- Petri Net: `rxnet.pn` — `Net` + `Transition` + `Arc`, greedy-sequential semantics

Both use the shared context:

- `context.inputs`: mutable live inputs (written by app/drivers)
- `context.latched_inputs`: snapshot used during guard evaluation

## Tests

```bash
cd python
uv run --extra dev pytest tests/ -v
```

All 56 tests should pass (runtime, FSM, PN — unit + semantic + callback).

## Examples

Interactive CLI examples mirror the C examples under `c/examples/`.  They
share two helpers in `examples/`:

- `app_driver.py` — simulated GPIO driver (host mock)
- `cli.py` — non-blocking CLI helper using a background stdin thread

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
uv run examples/fsm/02-auto/main.py
uv run examples/fsm/03-blink/main.py
uv run examples/fsm/04-mix/main.py

uv run examples/pn/01-light/main.py
uv run examples/pn/02-auto/main.py
uv run examples/pn/03-blink/main.py
uv run examples/pn/04-mix/main.py
```

Common commands in all interactive examples:

```
a           trigger button A
press a     same as 'a'
b           trigger button B
press b     same as 'b'
status      print current state
help        list available commands
quit/exit   exit
```

Additional commands depending on example:

```
timeout <a|b|c> <ms>   set auto-off timeout (02-auto, 04-mix)
freq <a|b|c> <hz>      set blink base frequency (03-blink)
freq <hz>              set blink base frequency (04-mix)
timeout <ms>           set auto-off timeout (04-mix)
```
