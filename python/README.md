# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.

The Python package has a shared internal runtime by phases:

- `rxnet.runtime`
- phase order per tick: latch inputs -> evaluate all nodes -> commit all nodes -> run deferred actions

Model frontends:

- FSM: `rxnet.fsm`
- Petri Net: `rxnet.pn`

Both use typed application inputs via context objects:

- `context.inputs`: mutable live inputs (written by app/drivers)
- `context.latched_inputs`: snapshot used during guard evaluation
- latch implementation: `context.latched_inputs = copy.copy(context.inputs)`

## Examples

Examples are under `python/examples` and split in:

- `model.py`: model definitions and guards/actions
- `system.py`: system-specific input writer/driver
- `main.py`: runtime wiring and execution loop

Run:

```bash
python examples/fsm/00-basic/main.py
python examples/pn/00-basic/main.py
```
