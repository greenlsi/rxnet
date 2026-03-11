# Implementation Plan: rxnet

## Overview

This implementation plan converts the `rxnet` design into discrete tasks for completing and validating the library. It follows an incremental approach: core runtime semantics first, model frontends second, integration examples third, and verification/quality automation last.

Tasks are marked as completed when they reflect the current repository state, and left pending where implementation, tests, or automation are still missing.

## Tasks

- [x] 1. Establish repository structure and multi-language layout
  - [x] 1.1 Create C runtime/frontend directories and public headers
    - Add `c/include/rxnet` and `c/src` layout for core runtime + FSM + PN
    - _Requirements: 4.1, 9.1_
  - [x] 1.2 Create Python package layout and modules
    - Add `python/rxnet` package with `runtime.py`, `fsm.py`, `pn.py`, `__init__.py`
    - _Requirements: 10.1, 10.2, 10.3, 10.4_
  - [x] 1.3 Add top-level and per-language README docs
    - Document phase model and basic execution usage
    - _Requirements: 11.1, 11.2_

- [x] 2. Implement shared synchronous core runtime (C)
  - [x] 2.1 Implement context lifecycle and input latching
    - Add `rx_context_init/free/latch_inputs`
    - Implement `inputs` + `latched_inputs` model and ownership boundaries
    - _Requirements: 1.1, 2.1, 2.2, 2.3, 2.5, 9.4_
  - [x] 2.2 Implement deferred action queue
    - Add enqueue/run APIs with dynamic capacity growth
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_
  - [x] 2.3 Implement generic runtime node orchestration
    - Add runtime init/add/tick/free and phase ordering
    - _Requirements: 1.2, 1.3, 1.4, 1.5, 4.1, 4.2, 4.3, 4.5_

- [x] 3. Implement FSM frontend (C)
  - [x] 3.1 Implement machine model and transition semantics
    - First-match transition selection in declaration order
    - Commit state and enqueue deferred action
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_
  - [x] 3.2 Implement FSM runtime wrapper
    - Runtime init/free, machine registration, tick wrapper
    - _Requirements: 9.1, 9.2_
  - [x] 3.3 Implement optional inputs projector hook
    - Add per-machine projector called before guard evaluation
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

- [x] 4. Implement Petri Net frontend (C)
  - [x] 4.1 Implement PN net model and transition execution
    - Evaluate fire flags from token availability + guards
    - Apply consume/produce deltas during commit
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5_
  - [x] 4.2 Implement PN validation and allocation safety
    - Validate arcs and reject invalid models
    - Handle allocation failures with cleanup
    - _Requirements: 8.1, 8.2, 8.3_
  - [x] 4.3 Implement PN runtime wrapper and lifecycle APIs
    - Runtime init/free, net add/tick, net free
    - _Requirements: 9.1, 9.2, 9.3, 9.5_

- [x] 5. Implement shared synchronous core runtime (Python)
  - [x] 5.1 Implement Python context with latched input snapshot
    - Use `copy.copy` snapshot semantics
    - _Requirements: 2.1, 2.2, 2.4_
  - [x] 5.2 Implement Python deferred action queue
    - Enqueue and execute post-commit actions
    - _Requirements: 3.1, 3.2, 3.3_
  - [x] 5.3 Implement Python generic runtime orchestration
    - Node registration and deterministic phase-ordered tick
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 4.1, 4.4_

- [x] 6. Implement FSM and PN frontends (Python)
  - [x] 6.1 Implement dataclass-based FSM API
    - `Machine` and `Transition` with first-match semantics
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 10.1_
  - [x] 6.2 Implement dataclass-based PN API
    - `Net`, `Transition`, `Arc` and commit delta application
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 10.2_
  - [x] 6.3 Implement Python validation behavior
    - Raise `IndexError` for out-of-range `place_id`
    - Raise `ValueError` for negative arc weight
    - _Requirements: 8.4, 8.5_
  - [x] 6.4 Expose package API aliases
    - Export modules and FSM-oriented aliases in `rxnet.__init__`
    - _Requirements: 10.3, 10.4, 10.5_

- [x] 7. Provide runnable examples and host integration patterns
  - [x] 7.1 Implement C FSM and PN console examples
    - Add deterministic sample flows for state/token updates
    - _Requirements: 11.1, 12.3, 12.4_
  - [x] 7.2 Implement C FSM 00-light host CLI and ESP-IDF variants
    - Shared input struct + machine-local projection pattern
    - _Requirements: 6.4, 11.4, 11.5, 12.3, 12.5_
  - [x] 7.3 Implement Python FSM and PN examples
    - Split model/system/main integration structure
    - _Requirements: 11.2, 11.3, 12.3, 12.5_

- [x] 8. Create baseline specification documents
  - [x] 8.1 Write requirements specification
    - Add `docs/specs/requirements.md` as source of truth for behavior
    - _Requirements: All requirements traceability_
  - [x] 8.2 Write design specification
    - Add `docs/specs/design.md` with architecture, interfaces, and properties
    - _Requirements: All requirements traceability_

- [ ] 9. Add C unit and integration tests
  - [ ] 9.1 Add runtime/context tests
    - Tick ordering, deferred queue, capacity limits, null-safety checks
    - _Requirements: 1.1, 1.5, 3.3, 4.3, 4.5, 9.5_
  - [ ] 9.2 Add FSM semantic tests
    - First-match ordering, projector invocation, no-match stability
    - _Requirements: 5.2, 5.3, 6.2_
  - [ ] 9.3 Add PN semantic/validation tests
    - Arc validation, token constraints, delta correctness, net lifecycle
    - _Requirements: 7.2, 7.4, 8.1, 8.2, 9.3_

- [ ] 10. Add Python unit and property-based tests
  - [ ] 10.1 Add runtime/frontend unit tests
    - Cover FSM and PN deterministic behavior and error paths
    - _Requirements: 1.5, 5.2, 7.2, 8.4, 8.5_
  - [ ]* 10.2 Add property tests for correctness properties
    - **Property 1: Phase Ordering Determinism**
    - **Property 7: FSM First-Match Transition Rule**
    - **Property 13: PN Commit Delta Correctness**
    - **Property 15: PN Invalid Arc Exceptions in Python**
    - _Requirements: 1.1, 5.2, 7.4, 8.4, 8.5_

- [ ] 11. Add C/Python semantic parity test harness
  - [ ] 11.1 Define shared scenario corpus
    - Scenarios for FSM and PN with expected traces
    - _Requirements: 1.5, 12.5_
  - [ ] 11.2 Implement cross-language conformance runner
    - Execute equivalent scenarios in C and Python and compare traces
    - _Requirements: 1.5, 10.4, 12.1_

- [ ] 12. Implement developer quality automation
  - [ ] 12.1 Add formatting/linting/type-check tools for Python
    - Configure `black`, `isort`, `flake8`, and `mypy`
    - _Requirements: 10.5 (maintainable API surface)_
  - [ ] 12.2 Add C static analysis and warning gates
    - Enforce warning-clean build in CI
    - _Requirements: 9.4, 12.1_
  - [ ] 12.3 Add pre-commit and CI checks
    - Ensure docs + tests + quality gates run on every PR
    - _Requirements: 11.1, 11.2_

- [ ] 13. Add packaging and release workflow
  - [ ] 13.1 Define Python packaging metadata and install flow
    - Add `pyproject.toml` and package build configuration
    - _Requirements: 10.3, 11.2_
  - [ ] 13.2 Define C library build/install workflow
    - Add canonical build targets for library + examples
    - _Requirements: 11.1, 12.3_
  - [ ] 13.3 Document versioning and compatibility rules
    - Tie release notes to requirements/design deltas
    - _Requirements: 12.1_

- [ ] 14. Final validation and readiness checkpoint
  - [ ] 14.1 Verify all correctness properties are covered by tests
    - Map tests to properties in `docs/specs/design.md`
    - _Requirements: All requirements_
  - [ ] 14.2 Verify requirements-to-implementation traceability
    - Confirm every requirement has implemented code + tests or explicit backlog status
    - _Requirements: All requirements_
  - [ ] 14.3 Establish “requirements-first” change workflow
    - PR checklist rule: update `requirements.md` before behavioral code changes
    - _Requirements: All requirements governance_

## Notes

- Tasks marked with `*` are optional for early MVP but recommended for robustness.
- This file tracks implementation status against current repository state as of March 11, 2026.
- Any behavior change should first update `docs/specs/requirements.md`, then `docs/specs/design.md`, then this task plan.
