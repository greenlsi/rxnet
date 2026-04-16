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

- [x] 7. Add C/Python semantic parity test harness
  - [x] 7.1 Define shared scenario corpus
    - Three scenarios: `fsm_light`, `fsm_first_match`, `pn_light` (same as C corpus)
    - _Requirements: 1.5_
  - [x] 7.2 Implement cross-language conformance runner (Python side)
    - `python/tests/parity_runner.py` outputs identical 15-line trace to C runner
    - `tests/parity/run_parity.sh` diffs both; CI `parity` job runs the check
    - _Requirements: 1.5, 8.4_

- [x] 8. Implement developer quality automation
  - [x] 8.1 Add formatting/linting/type-check tools
    - `ruff` (lint + format) and `mypy` configured in `pyproject.toml` `[tool.ruff]`/`[tool.mypy]`
    - Run: `uv run --extra dev ruff check rxnet/ tests/` and `uv run --extra dev mypy rxnet/`
    - _Requirements: 8.5_
  - [x] 8.2 Add pre-commit and CI checks
    - `.github/workflows/ci.yml`: `python-tests` job runs ruff + mypy + pytest on every PR
    - `.pre-commit-config.yaml`: ruff + ruff-format hooks for Python files
    - _Requirements: 9.1, 9.2_

- [x] 9. Add Python packaging and release workflow
  - [x] 9.1 Define Python packaging metadata and install flow
    - `pyproject.toml` with hatchling build backend; `dev` extras include pytest, ruff, mypy
    - Run tests: `uv run --extra dev pytest tests/ -v`
    - _Requirements: 8.3, 9.1_
  - [x] 9.2 Document versioning and compatibility rules
    - Versioning policy: increment `version` in `pyproject.toml` on any public API change.
      PR checklist: requirements → design → tasks → code.

- [x] 10. Final validation and readiness checkpoint
  - [x] 10.1 Verify all correctness properties are covered by tests
    - 56 unit tests cover all design properties (Context, Runtime, FSM, PN semantics)
    - Parity runner covers Property 1 (phase ordering), 6 (first-match), 11 (PN delta)
  - [x] 10.2 Verify requirements-to-implementation traceability
    - All requirements §1–§8 have implemented code + pytest coverage
    - Parity harness covers §1.5 (cross-language conformance)
  - [x] 10.3 Establish "requirements-first" change workflow
    - PR rule in `.github/workflows/ci.yml` and documented here:
      update `requirements.md` → `design.md` → `tasks.md` before behavioral code changes

## Notes

- Tasks marked with `*` are optional for early MVP but recommended for robustness.
- This file tracks implementation status against current repository state as of April 2026.
- Any behavior change should first update `docs/specs/python/requirements.md`, then `docs/specs/python/design.md`, then this task plan.
