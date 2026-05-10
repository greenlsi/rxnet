# Requirements Document: rxnet — Python Implementation

## Introduction

This document specifies the requirements for the Python implementation of `rxnet`, a reactive synchronous runtime library that supports two model families: Finite State Machines (FSM) and Petri Nets (PN). The Python implementation provides a shared phase-based execution core and language frontend using dataclasses and standard-library modules.

The goal of this document is to capture what the Python library provides today, so future changes can be validated and reflected here first.

## Glossary

- **RXNET_System**: The complete `rxnet` Python library surface
- **Core_Runtime**: Shared synchronous execution engine with phase-based ticking
- **Context**: Execution context containing live inputs, latched inputs, and deferred actions
- **Node**: Runtime unit with `evaluate` and `commit` phases
- **Deferred_Action**: Action scheduled during commit and executed after all commits
- **Activation_Group**: Set of nodes activated at the same logical instant
- **WCET**: Maximum measured complete node tick time, from latch start to dump end
- **FSM_Runtime**: FSM frontend wrapper over the core runtime (`rxnet.fsm.Runtime`)
- **FSM_Machine**: State machine node with transitions, guards, and optional actions (`rxnet.fsm.Machine`)
- **FSM_Transition**: Rule from `from_state` to `to_state`, with optional guard/action (`rxnet.fsm.Transition`)
- **PN_Runtime**: Petri Net frontend wrapper over the core runtime (`rxnet.pn.Runtime`)
- **PN_Net**: Petri net node with places, transitions (`rxnet.pn.Net`)
- **PN_Transition**: Petri transition with consume/produce arcs, optional guard/action (`rxnet.pn.Transition`)
- **Arc**: Petri arc with `place_id` and `weight` (`rxnet.pn.Arc`)
- **Tick**: One complete synchronous cycle: latch -> evaluate -> commit -> dump, with deferred action dispatch between commit and dump

## Requirements

### Requirement 1: Shared Synchronous Phase Model

**User Story:** As a model developer, I want deterministic phase-based execution, so that all nodes observe a consistent input snapshot and state updates happen predictably.

#### Acceptance Criteria

1. WHEN a tick starts, THE Core_Runtime SHALL latch input data before node evaluation
2. WHEN latching completes, THE Core_Runtime SHALL evaluate all registered nodes
3. WHEN evaluation completes, THE Core_Runtime SHALL commit all registered nodes
4. WHEN commit completes, THE Core_Runtime SHALL execute deferred actions and clear the queue before dump
5. WHEN deferred action dispatch completes, THE Core_Runtime SHALL dump outputs for all nodes
6. THE Tick order SHALL remain `latch -> evaluate -> commit -> dump` in the Python implementation, with deferred action dispatch between commit and dump
7. WHEN an executor activates multiple nodes at one instant, THE Context SHALL expose the same `activation_us` value to every node in that activation group

### Requirement 2: Context and Input Snapshot Handling

**User Story:** As an integrator, I want separate live and latched inputs, so that guards read a stable snapshot during each tick.

#### Acceptance Criteria

1. THE Context SHALL expose mutable live inputs for application/driver updates
2. THE Context SHALL expose latched inputs used during evaluation
3. WHEN ticking, THE runtime SHALL update `latched_inputs` using `copy.copy(inputs)`

### Requirement 3: Deferred Action Execution

**User Story:** As a runtime user, I want actions to run after all commits, so that side effects are deferred and model state commits are consistent.

#### Acceptance Criteria

1. WHEN a node commits with a proposed action, THE Context SHALL enqueue the action for deferred execution
2. THE Core_Runtime SHALL run deferred actions only after all node commits in the tick
3. THE deferred action queue SHALL be cleared after execution

### Requirement 4: Generic Node Runtime API

**User Story:** As a library user, I want a generic runtime abstraction, so that different model families can share one execution engine.

#### Acceptance Criteria

1. THE Node contract SHALL include `evaluate` and `commit` callbacks
2. THE runtime SHALL support dynamic node list growth without explicit capacity configuration

### Requirement 5: FSM Semantics

**User Story:** As an FSM author, I want first-match transition evaluation with deferred actions, so that machine behavior is deterministic and clear.

#### Acceptance Criteria

1. WHEN an FSM machine evaluates, THE machine SHALL reset `next_state` to current state and clear proposed action
2. THE FSM evaluator SHALL scan transitions in declaration order and select the first transition whose `from_state` matches and whose guard is true (or absent)
3. WHEN no transition matches, THE machine SHALL remain in the current state
4. WHEN committing, THE machine SHALL update current state to `next_state`
5. WHEN a transition action exists, THE action SHALL be enqueued as a deferred action (not executed inline during evaluate/commit)

### Requirement 6: Petri Net Semantics

**User Story:** As a Petri Net author, I want place/transition execution with guards and arc validation, so that token flow is modeled consistently.

#### Acceptance Criteria

1. WHEN a PN net evaluates, THE net SHALL copy `places` into `next_places` and reset transition fire flags
2. THE PN evaluator SHALL mark a transition as fireable only if consume/produce arcs are valid and consume tokens are available
3. WHEN a transition guard exists, THE transition SHALL fire only if the guard returns true
4. WHEN committing, THE net SHALL apply consume/produce deltas for each fire-flagged transition
5. WHEN a transition action exists, THE action SHALL be enqueued as a deferred action

### Requirement 7: PN Data Validation and Error Behavior

**User Story:** As a developer, I want invalid Petri model data to be detected, so that configuration errors fail early.

#### Acceptance Criteria

1. WHEN evaluating a PN net with an invalid arc `place_id`, THE runtime SHALL raise `IndexError`
2. WHEN evaluating a PN net with a negative arc weight, THE runtime SHALL raise `ValueError`

### Requirement 8: Python Frontend API Surface

**User Story:** As a Python developer, I want ergonomic dataclass-based APIs, so that models are easy to define and run.

#### Acceptance Criteria

1. THE FSM frontend SHALL expose `Machine` and `Transition` dataclasses with optional guard/action callbacks
2. THE PN frontend SHALL expose `Net`, `Transition`, and `Arc` dataclasses
3. THE runtime wrappers (`rxnet.fsm.Runtime`, `rxnet.pn.Runtime`) SHALL expose `context`, model registration, and `tick()`
4. THE root package SHALL expose `fsm`, `pn`, `runtime`, and FSM-oriented aliases currently defined in `rxnet.__init__`
5. THE implementation SHALL rely only on standard-library modules present in the codebase (`dataclasses`, `typing`, `copy`)

### Requirement 9: Integration Patterns and Examples

**User Story:** As a user evaluating the library, I want runnable examples in Python, so that I can understand expected integration patterns.

#### Acceptance Criteria

1. THE repository SHALL include runnable Python examples for FSM and PN under `python/examples`
2. THE example structure SHALL separate model definition, system input writer, and runtime wiring (`model.py`, `system.py`, `main.py`)

### Requirement 10: Execution Model Constraints

**User Story:** As a platform engineer, I want clear operational constraints, so that I can integrate `rxnet` correctly in larger systems.

#### Acceptance Criteria

1. THE runtime SHALL be single-threaded per runtime instance unless external synchronization is provided by the integrator
2. THE library SHALL not include built-in networking, persistence, or REST APIs
3. THE library SHALL leave manual tick frequency/loop ownership to the host application
4. THE library SHALL leave I/O side effects to user-defined action callbacks
5. THE library SHALL support using one shared typed input structure across multiple model nodes in the same runtime context
6. THE Core_Runtime SHALL NOT materialise a hyperperiod activation table
7. THE cyclic, cooperative, and thread executors SHALL own their scheduling state
8. THE cyclic, cooperative, and thread executors SHALL expose schedulability analysis based on measured WCET values
9. THE runtime SHALL measure and retain the maximum critical-section access time for each task/resource pair
10. THE thread executor SHALL include shared-resource blocking in response-time analysis using lower-priority critical sections without reusing a task or resource in the same blocking combination
