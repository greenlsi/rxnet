# rxnet (Python)

`rxnet` is a small synchronous runtime for reactive models.

This version includes a clean finite-state-machine core with:

- integer states
- transitions stored in a list
- guard and action functions
- global input latching
- deferred actions executed after the global state commit

## Model

A transition is:

```python
Transition(from_state, to_state, guard=None, action=None)
```

Semantics:

1. stage external inputs
2. `tick()` latches all inputs at once
3. every machine evaluates transitions against the same snapshot
4. all next states are committed together
5. deferred actions run after the commit

## Guard and action signatures

```python
def guard(ctx, user) -> bool: ...
def action(ctx, user) -> None: ...
```

## Example

```bash
python example.py
```
