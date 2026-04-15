# Requirements Document: rxnet — C Implementation

## Introduction

This document specifies the requirements for the C implementation of `rxnet`, a reactive synchronous runtime library that supports two model families: Finite State Machines (FSM) and Petri Nets (PN). The C implementation provides a shared phase-based execution core and language frontend in C11.

The goal of this document is to capture what the C library provides today, so future changes can be validated and reflected here first.

## Glossary

- **RXNET_System**: The complete `rxnet` C library surface
- **Core_Runtime**: Shared synchronous execution engine with phase-based ticking
- **Context**: Execution context containing live inputs, latched inputs, and deferred actions
- **Node**: Runtime unit with `evaluate` and `commit` phases
- **Deferred_Action**: Action scheduled during commit and executed after all commits
- **FSM_Runtime**: FSM frontend wrapper over the core runtime
- **FSM_Machine**: State machine node with transitions, guards, and optional actions
- **FSM_Transition**: Rule from `from_state` to `to_state`, with optional guard/action
- **PN_Runtime**: Petri Net frontend wrapper over the core runtime
- **PN_Net**: Petri net node with places, transitions, and firing flags
- **PN_Transition**: Petri transition with consume/produce arcs, optional guard/action
- **Arc**: Petri arc with `place_id` and `weight`
- **Tick**: One complete synchronous cycle: latch -> evaluate -> commit -> deferred actions
- **Latched_Inputs**: Immutable snapshot of current inputs used during one tick
- **Config_Header**: Compile-time configuration header (`rxnet/config.h`) that defines fixed runtime capacities

## Requirements

### Requirement 1: Shared Synchronous Phase Model

**User Story:** As a model developer, I want deterministic phase-based execution, so that all nodes observe a consistent input snapshot and state updates happen predictably.

#### Acceptance Criteria

1. WHEN a tick starts, THE Core_Runtime SHALL latch input data before node evaluation
2. WHEN latching completes, THE Core_Runtime SHALL evaluate all registered nodes
3. WHEN evaluation completes, THE Core_Runtime SHALL commit all registered nodes
4. WHEN commit completes, THE Core_Runtime SHALL execute deferred actions and clear the queue
5. THE Tick order SHALL remain `latch -> evaluate -> commit -> deferred actions` in the C implementation

### Requirement 2: Context and Input Snapshot Handling

**User Story:** As an integrator, I want separate live and latched inputs, so that guards read a stable snapshot during each tick.

#### Acceptance Criteria

1. THE Context SHALL expose mutable live inputs for application/driver updates
2. THE Context SHALL expose latched inputs used during evaluation
3. WHEN ticking, THE runtime SHALL copy `inputs_size` bytes from `inputs` to `latched_inputs`
4. THE runtime SHALL keep ownership of the live inputs buffer in the application (runtime does not free it)
5. WHEN initializing context, THE provided `inputs_size` SHALL be less than or equal to the compile-time configured maximum input size

### Requirement 3: Deferred Action Execution

**User Story:** As a runtime user, I want actions to run after all commits, so that side effects are deferred and model state commits are consistent.

#### Acceptance Criteria

1. WHEN a node commits with a proposed action, THE Context SHALL enqueue the action for deferred execution
2. THE Core_Runtime SHALL run deferred actions only after all node commits in the tick
3. THE deferred action queue SHALL be cleared after execution
4. THE deferred-action queue capacity SHALL be fixed at initialization from compile-time configuration (`rxnet/config.h`)
5. WHEN enqueue parameters are invalid (`ctx` or function is null), THE API SHALL return an error code (`-1`)
6. WHEN deferred-action capacity is exhausted, THE enqueue API SHALL return `-1` without allocating memory

### Requirement 4: Generic Node Runtime API

**User Story:** As a library user, I want a generic runtime abstraction, so that different model families can share one execution engine.

#### Acceptance Criteria

1. THE Node contract SHALL include `evaluate` and `commit` callbacks
2. THE runtime SHALL support runtime initialization with explicit node capacity
3. WHEN adding nodes beyond configured capacity, THE runtime SHALL reject the operation with `-1`
4. WHEN `tick` is called with invalid runtime/context, THE API SHALL return `-1`
5. WHEN initializing runtime, THE requested node capacity SHALL be less than or equal to the compile-time configured maximum node capacity

### Requirement 5: FSM Semantics

**User Story:** As an FSM author, I want first-match transition evaluation with deferred actions, so that machine behavior is deterministic and clear.

#### Acceptance Criteria

1. WHEN an FSM machine evaluates, THE machine SHALL reset `next_state` to current state and clear proposed action
2. THE FSM evaluator SHALL scan transitions in declaration order and select the first transition whose `from_state` matches and whose guard is true (or absent)
3. WHEN no transition matches, THE machine SHALL remain in the current state
4. WHEN committing, THE machine SHALL update current state to `next_state`
5. WHEN a transition action exists, THE action SHALL be enqueued as a deferred action (not executed inline during evaluate/commit)

### Requirement 6: FSM Shared Input Access

**User Story:** As an embedded integrator, I want multiple machines to share one latched input snapshot, so that guard evaluation is simple and deterministic.

#### Acceptance Criteria

1. THE runtime SHALL allow multiple FSM machines to share one context input struct
2. THE FSM guard/action callbacks SHALL receive the shared latched context and machine user payload
3. FSM guard evaluation SHALL proceed directly with transition guard checks

### Requirement 7: Petri Net Semantics

**User Story:** As a Petri Net author, I want place/transition execution with guards and arc validation, so that token flow is modeled consistently.

#### Acceptance Criteria

1. WHEN a PN net evaluates, THE net SHALL copy `places` into `next_places` and reset transition fire flags
2. THE PN evaluator SHALL mark a transition as fireable only if consume/produce arcs are valid and consume tokens are available
3. WHEN a transition guard exists, THE transition SHALL fire only if the guard returns true
4. WHEN committing, THE net SHALL apply consume/produce deltas for each fire-flagged transition
5. WHEN a transition action exists, THE action SHALL be enqueued as a deferred action

### Requirement 8: PN Data Validation and Error Behavior

**User Story:** As a developer, I want invalid Petri model data to be detected, so that configuration errors fail early.

#### Acceptance Criteria

1. WHEN initializing a PN net, THE API SHALL validate all consume/produce arc lists and reject invalid models with `-1`
2. THE PN arc validator SHALL reject arcs with out-of-range `place_id` or negative `weight`
3. WHEN PN net allocation fails, THE API SHALL free partial allocations and return `-1`

### Requirement 9: Frontend Lifecycle and Memory Management

**User Story:** As a C integrator, I want explicit initialization and cleanup APIs, so that runtime memory is managed safely.

#### Acceptance Criteria

1. THE API SHALL provide runtime init/free functions for both FSM and PN frontends, and optional create/destroy convenience constructors
2. THE API SHALL provide model registration functions (`add_machine`, `add_net`) before ticking
3. THE API SHALL provide model-specific cleanup for PN net internal buffers (`rx_pn_net_free`)
4. WHEN freeing runtime/context, THE API SHALL reset internal pointers and counters to neutral values
5. WHEN null pointers are passed to free functions, THE functions SHALL return safely without crashing
6. FOR entities that expose `_create`, THE API SHALL expose matching `_destroy` and `_init` entry points

### Requirement 10: Integration Patterns and Examples

**User Story:** As a user evaluating the library, I want runnable examples in C, so that I can understand expected integration patterns.

#### Acceptance Criteria

1. THE repository SHALL include runnable C examples for FSM and PN under `c/examples`
2. THE repository SHALL include an ESP-IDF FSM integration example with periodic ticking and ISR-driven input updates
3. THE host CLI FSM example (`c/examples/fsm/00-light/main_cli.c`) SHALL keep runtime wiring minimal (machine creation/registration + periodic tick loop), delegating command parsing to a dedicated CLI FSM

### Requirement 11: Reusable CLI FSM Utility (Example Layer)

**User Story:** As an example integrator, I want a reusable CLI FSM utility with command registration and contextual user data, so that multiple examples can share the same terminal command loop behavior without duplicating logic.

#### Acceptance Criteria

1. THE example layer SHALL provide a reusable CLI FSM module independent from the `00-light` domain model
2. THE CLI FSM module SHALL support registering multiple commands by name via a public registration API
3. EACH registered command SHALL support a per-command `user_data` payload passed to the command callback
4. THE CLI FSM machine data SHALL expose a machine-level `user_data` pointer for integration-specific shared context
5. THE CLI FSM module SHALL encapsulate terminal raw mode entry and restoration

### Requirement 12: Node Embedding and Typed Polymorphism

**User Story:** As a C integrator, I want model structs to embed a base node type, so that runtime polymorphism is type-structured and avoids untyped `void* self` adapters.

#### Acceptance Criteria

1. THE `rx_node` interface SHALL support polymorphism without a `void* self` field
2. THE FSM machine struct SHALL embed `rx_node` as its base member
3. THE PN net struct SHALL embed `rx_node` as its base member
4. THE runtime node registry SHALL store node pointers to embedded base nodes
5. THE tick loop SHALL invoke `evaluate`/`commit` through the embedded node base
6. THE frontend runtime wrappers (`rx_fsm_runtime`, `rx_pn_runtime`) SHALL embed `rx_runtime` as first member to allow safe upcast to base runtime pointer

### Requirement 13: Init/Create API Pattern

**User Story:** As a C developer, I want both `_init` and `_create` APIs, so that I can choose deterministic preallocated memory or convenience allocation consistently across entities.

#### Acceptance Criteria

1. THE API SHALL keep `_init` functions for caller-owned memory initialization
2. THE API SHALL provide `_create` functions that allocate memory and delegate to `_init`
3. THE API SHALL provide matching `_destroy` functions for `_create` allocations
4. THE `_create` APIs SHALL perform allocation only during creation time and fail with null on allocation/init errors
5. THE `_destroy` APIs SHALL be null-safe

### Requirement 14: Execution Model Constraints

**User Story:** As a platform engineer, I want clear operational constraints, so that I can integrate `rxnet` correctly in larger systems.

#### Acceptance Criteria

1. THE runtime SHALL be single-threaded per runtime instance unless external synchronization is provided by the integrator
2. THE library SHALL not include built-in networking, persistence, or REST APIs
3. THE library SHALL leave scheduling policy (tick frequency/loop ownership) to the host application
4. THE library SHALL leave I/O side effects to user-defined action callbacks
5. THE library SHALL support using one shared typed input structure across multiple model nodes in the same runtime context
6. THE tick path (`latch`, `evaluate`, `commit`, `run deferred actions`) SHALL perform no heap allocation or reallocation
7. THE fixed runtime capacities used by the tick path SHALL be configurable through `rxnet/config.h`
