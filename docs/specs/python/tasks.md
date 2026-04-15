# Implementation Plan: rxnet — Python Implementation

## Overview

This implementation plan converts the `rxnet` Python design into discrete tasks for completing and validating the library. It follows an incremental approach: core runtime semantics first, model frontends second, integration examples third, and verification/quality automation last.

Tasks are marked as completed when they reflect the current repository state, and left pending where implementation, tests, or automation are still missing.

## Tasks

- [x] 1. Establish Python repository structure
  - [x] 1.1 Create Python package layout and modules
    - Add `python/rxnet` package with `runtime.py`, `fsm.py`, `pn.py`, `__init__.py`
    - _Requirements: 8.1, 8.2, 8.3, 8.4_
  - [x] 1.2 Add top-level README docs
    - Document phase model and basic execution usage
    - _Requirements: 9.1_

- [x] 2. Implement shared synchronous core runtime
  - [x] 2.1 Implement Python context with latched input snapshot
    - Use `copy.copy` snapshot semantics
    - _Requirements: 2.1, 2.2, 2.3_
  - [x] 2.2 Implement Python deferred action queue
    - Enqueue and execute post-commit actions
    - _Requirements: 3.1, 3.2, 3.3_
  - [x] 2.3 Implement Python generic runtime orchestration
    - Node registration and deterministic phase-ordered tick
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 4.1, 4.2_

- [x] 3. Implement FSM and PN frontends
  - [x] 3.1 Implement dataclass-based FSM API
    - `Machine` and `Transition` with first-match semantics
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 8.1_
  - [x] 3.2 Implement dataclass-based PN API
    - `Net`, `Transition`, `Arc` and commit delta application
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 8.2_
  - [x] 3.3 Implement Python validation behavior
    - Raise `IndexError` for out-of-range `place_id`
    - Raise `ValueError` for negative arc weight
    - _Requirements: 7.1, 7.2_
  - [x] 3.4 Expose package API aliases
    - Export modules and FSM-oriented aliases in `rxnet.__init__`
    - _Requirements: 8.3, 8.4, 8.5_

- [x] 4. Provide runnable Python examples and host integration patterns
  - [x] 4.1 Implement Python FSM and PN examples
    - Split model/system/main integration structure
    - _Requirements: 9.1, 9.2, 10.3_
  - [x] 4.2 Implement interactive CLI FSM examples (01-light, 02-auto, 03-blink, 04-mix)
    - Equivalent to C `examples/fsm/00-light` through `fsm/03-mix`
    - Shared `app_driver.py` (host-mock GPIO) + `cli.py` (non-blocking stdin)
    - _Requirements: 9.1, 9.2, 10.3_
  - [x] 4.3 Implement interactive CLI PN examples (01-light, 02-auto, 03-blink, 04-mix)
    - Equivalent to C `examples/pn/01-light` through `pn/04-mix`
    - REQUEST places, AUTO_OFF_DUE signal place, TOGGLE_DUE signal place
    - _Requirements: 9.1, 9.2, 10.3_

- [x] 5. Create baseline Python specification documents
  - [x] 5.1 Write Python requirements specification
    - Add `docs/specs/python/requirements.md` as source of truth for Python behavior
  - [x] 5.2 Write Python design specification
    - Add `docs/specs/python/design.md` with architecture, interfaces, and properties

- [x] 6. Add Python unit and property-based tests
  - [x] 6.1 Add runtime/frontend unit tests
    - 56 tests covering runtime (Context + Runtime), FSM (lifecycle, transitions, guards, actions, callbacks), and PN (lifecycle, firing, guards, actions, greedy-sequential, callbacks)
    - _Requirements: 1.5, 5.2, 6.2, 7.1, 7.2_
  - [ ]* 6.2 Add property tests for correctness properties
    - **Property 1: Phase Ordering Determinism**
    - **Property 6: FSM First-Match Transition Rule**
    - **Property 11: PN Commit Delta Correctness**
    - **Property 12: PN Invalid Arc Exceptions**
    - _Requirements: 1.1, 5.2, 6.4, 7.1, 7.2_

- [ ] 7. Add C/Python semantic parity test harness
  - [ ] 7.1 Define shared scenario corpus
    - Scenarios for FSM and PN with expected traces
    - _Requirements: 1.5_
  - [ ] 7.2 Implement cross-language conformance runner (Python side)
    - Execute equivalent scenarios and compare traces with C output
    - _Requirements: 1.5, 8.4_

- [ ] 8. Implement developer quality automation
  - [ ] 8.1 Add formatting/linting/type-check tools
    - Configure `black`, `isort`, `flake8`, and `mypy`
    - _Requirements: 8.5_
  - [ ] 8.2 Add pre-commit and CI checks
    - Ensure docs + tests + quality gates run on every PR
    - _Requirements: 9.1, 9.2_

- [x] 9. Add Python packaging and release workflow
  - [x] 9.1 Define Python packaging metadata and install flow
    - `pyproject.toml` with hatchling build backend and `dev` extras (pytest)
    - Run tests: `uv run --extra dev pytest tests/ -v`
    - _Requirements: 8.3, 9.1_
  - [ ] 9.2 Document versioning and compatibility rules
    - Tie release notes to requirements/design deltas

- [ ] 10. Final validation and readiness checkpoint
  - [ ] 10.1 Verify all correctness properties are covered by tests
    - Map tests to properties in `docs/specs/python/design.md`
  - [ ] 10.2 Verify requirements-to-implementation traceability
    - Confirm every requirement has implemented code + tests or explicit backlog status
  - [ ] 10.3 Establish "requirements-first" change workflow
    - PR checklist rule: update `docs/specs/python/requirements.md` before behavioral code changes

## Notes

- Tasks marked with `*` are optional for early MVP but recommended for robustness.
- This file tracks implementation status against current repository state as of April 2026.
- Any behavior change should first update `docs/specs/python/requirements.md`, then `docs/specs/python/design.md`, then this task plan.
