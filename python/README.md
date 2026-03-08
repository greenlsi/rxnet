# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.

The Python package provides two modules:

- FSM: `rxnet.fsm`
- Petri Net: `rxnet.pn`

Both use typed application inputs via context objects:

- `context.inputs`: mutable live inputs (written by app/drivers)
- `context.latched_inputs`: snapshot used during guard evaluation
- latch implementation: `context.latched_inputs = copy.copy(context.inputs)`

Tick semantics:

1. app updates `context.inputs`
2. `tick` latches with `copy.copy`
3. guards evaluate against `latched_inputs`
4. state/marking commit happens globally
5. deferred actions run after commit

## Examples

```bash
python example.py
python pn_example.py
```
