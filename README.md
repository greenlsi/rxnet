# rxnet

Reactive synchronous runtime with two model families (FSM and Petri Net) sharing one internal phase-based execution model.

Common tick phases:

1. read/latch input snapshot
2. evaluate nodes
3. commit updates
4. run deferred actions

C:

- Core runtime: `c/include/rxnet/runtime.h`, `c/src/runtime.c`
- FSM frontend: `c/include/rxnet/fsm.h`, `c/src/fsm.c`
- Petri frontend: `c/include/rxnet/pn.h`, `c/src/pn.c`

Python:

- Core runtime: `python/rxnet/runtime.py`
- FSM frontend: `python/rxnet/fsm.py`
- Petri frontend: `python/rxnet/pn.py`
- Examples: `python/examples/fsm/*` and `python/examples/pn/*`
