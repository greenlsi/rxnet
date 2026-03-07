# rxnet (C)

`rxnet` is a small synchronous runtime for reactive models.

This C core currently includes finite-state machines with:

- integer states
- transitions stored in arrays
- guard and action function pointers
- global input latching
- deferred actions executed after the global state commit

## Build the example

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude src/rxnet.c examples/example.c -o example
./example
```

## Transition

```c
rx_transition t = {
    .from_state = IDLE,
    .to_state = RUNNING,
    .guard = start_pressed,
    .action = motor_on,
};
```

## Semantics

1. stage external inputs
2. `rx_tick()` latches all inputs at once
3. every machine evaluates transitions against the same snapshot
4. all next states are committed together
5. deferred actions run after the commit
