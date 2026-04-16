# Implementation Plan: rxnet — C Implementation

## Overview

This implementation plan converts the `rxnet` C design into discrete tasks for completing and validating the library. It follows an incremental approach: core runtime semantics first, model frontends second, integration examples third, and verification/quality automation last.

Tasks are marked as completed when they reflect the current repository state, and left pending where implementation, tests, or automation are still missing.

## Tasks

- [x] 1. Establish C repository structure
  - [x] 1.1 Create C runtime/frontend directories and public headers
    - Add `c/include/rxnet` and `c/src` layout for core runtime + FSM + PN
    - _Requirements: 4.1, 9.1_
  - [x] 1.2 Add top-level README docs
    - Document phase model and basic execution usage
    - _Requirements: 10.1_

- [x] 2. Implement shared synchronous core runtime
  - [x] 2.1 Implement context lifecycle and input latching
    - Add `rx_context_init/free/latch_inputs`
    - Implement `inputs` + `latched_inputs` model and ownership boundaries
    - _Requirements: 1.1, 2.1, 2.2, 2.3, 2.4, 9.4_
  - [x] 2.2 Implement deferred action queue
    - Add enqueue/run APIs with fixed-capacity bounded behavior
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 14.6_
  - [x] 2.3 Implement generic runtime node orchestration
    - Add runtime init/add/tick/free and phase ordering
    - _Requirements: 1.2, 1.3, 1.4, 1.5, 4.1, 4.2, 4.3, 4.4_

- [x] 3. Implement FSM frontend
  - [x] 3.1 Implement machine model and transition semantics
    - First-match transition selection in declaration order
    - Commit state and enqueue deferred action
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_
  - [x] 3.2 Implement FSM runtime wrapper
    - Runtime init/free, machine registration, tick wrapper
    - _Requirements: 9.1, 9.2_
  - [x] 3.3 Implement shared-input guard access
    - Read shared latched inputs directly from FSM guards/actions
    - _Requirements: 6.1, 6.2, 6.3_

- [x] 4. Implement Petri Net frontend
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

- [x] 5. Provide runnable C examples and host integration patterns
  - [x] 5.1 Implement FSM and PN console examples
    - Add deterministic sample flows for state/token updates
    - _Requirements: 10.1, 14.3, 14.4_
  - [x] 5.2 Implement FSM 00-light host CLI and ESP-IDF variants
    - Shared input struct consumed directly by machine guards
    - _Requirements: 6.1, 10.2, 10.3, 14.3_
  - [x] 5.3 Implement FSM CLI examples 01-auto, 02-blink, 03-mix
    - Three-state blink, auto-off timer, and mixed runtime
    - _Requirements: 10.2, 10.3_
  - [x] 5.4 Implement PN CLI examples 01-light, 02-auto, 03-blink, 04-mix
    - REQUEST places, AUTO_OFF_DUE signal place, TOGGLE_DUE signal place, dual runtime
    - _Requirements: 10.2, 10.3_

- [x] 6. Create baseline C specification documents
  - [x] 6.1 Write C requirements specification
    - Add `docs/specs/c/requirements.md` as source of truth for C behavior
  - [x] 6.2 Write C design specification
    - Add `docs/specs/c/design.md` with architecture, interfaces, and properties

- [x] 7. Add C unit and integration tests
  - [x] 7.1 Add runtime/context tests
    - 26 tests: tick ordering, deferred queue FIFO + capacity limits, null-safety checks
    - _Requirements: 1.1, 1.5, 3.3, 4.3, 4.4, 9.5_
  - [x] 7.2 Add FSM semantic tests
    - 21 tests: first-match ordering, false-guard skip, user-data guard reads, no-match stability, deferred action timing
    - _Requirements: 5.2, 5.3, 6.2_
  - [x] 7.3 Add PN semantic/validation tests
    - 24 tests: arc validation, token availability, delta correctness, guard/action wiring, net lifecycle
    - _Requirements: 7.2, 7.4, 8.1, 8.2, 9.3_

- [x] 8. Add C/Python semantic parity test harness
  - [x] 8.1 Define shared scenario corpus
    - Three scenarios: `fsm_light` (toggle), `fsm_first_match`, `pn_light` (token toggle)
    - Documented in `c/tests/parity_runner.c` and `python/tests/parity_runner.py`
    - _Requirements: 1.5_
  - [x] 8.2 Implement cross-language conformance runner (C side)
    - `c/tests/parity_runner.c` emits 15 lines; `tests/parity/run_parity.sh` diffs C vs Python
    - Makefile `parity` target builds the runner; CI `parity` job runs the check
    - _Requirements: 1.5_

- [x] 9. Implement developer quality automation
  - [x] 9.1 Add C static analysis and warning gates
    - Build uses `-Wall -Wextra -Wpedantic`; `make test` is warning-clean
    - `.github/workflows/ci.yml` `c-tests` job enforces this on every PR
    - _Requirements: 9.4, 14.1_
  - [x] 9.2 Add pre-commit and CI checks
    - `.github/workflows/ci.yml`: `c-tests`, `python-tests`, `parity` jobs on push/PR
    - `.pre-commit-config.yaml`: ruff + C test hook
    - _Requirements: 10.1_

- [x] 10. Add C library build/install workflow
  - [x] 10.1 Define canonical build targets for library + examples
    - Makefile builds all FSM + PN examples: fsm_{light,auto,blink,mix}_cli + pn_{01,02,03,04}
    - _Requirements: 10.1, 14.3_
  - [x] 10.2 Document versioning and compatibility rules
    - Versioning policy: increment `version` in `docs/specs/c/requirements.md` preamble on any
      public API change; bump `RXNET_VERSION_*` macros in `config.h` for releases.
      PR checklist: requirements → design → tasks → code.

- [x] 11. Final validation and readiness checkpoint
  - [x] 11.1 Verify all correctness properties are covered by tests
    - 71 C unit tests (test_runtime, test_fsm, test_pn) cover all design properties
    - 15-line parity trace covers cross-language semantic equivalence (Property 1, 5, 11)
  - [x] 11.2 Verify requirements-to-implementation traceability
    - All requirements §1–§14 have corresponding implementation + `make test` coverage
    - Parity harness covers §1.5 (cross-language conformance)
  - [x] 11.3 Establish "requirements-first" change workflow
    - PR rule encoded in `.github/workflows/ci.yml` and documented here:
      update `requirements.md` → `design.md` → `tasks.md` before behavioral code changes

- [x] 12. Reflect reusable CLI FSM changes in specifications
  - [x] 12.1 Update requirements for reusable CLI FSM contract
    - Capture command registry API, raw mode encapsulation, and user data behavior
    - _Requirements: 10.3, 11.1, 11.2, 11.3, 11.4, 11.5_
  - [x] 12.2 Update design for reusable CLI FSM architecture and interfaces
    - Document generic CLI FSM responsibilities and public C interfaces
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

- [x] 13. Enforce no dynamic allocation during tick path
  - [x] 13.1 Update deferred queue implementation to fixed-capacity behavior
    - Remove runtime `realloc` from deferred enqueue path and reject overflow deterministically
    - _Requirements: 3.4, 3.6, 14.6_
  - [x] 13.2 Update specs and traceability for bounded deferred queue
    - Align requirement/design/task wording with fixed-capacity queue model
    - _Requirements: 3.4, 3.6, 14.6_
  - [x] 13.3 Build-check C examples after memory policy change
    - Verify FSM and PN examples still compile cleanly
    - _Requirements: 10.1, 9.1_

- [x] 14. Configure fixed C runtime arrays via config header
  - [x] 14.1 Add `rxnet/config.h` with runtime capacity macros
    - Define compile-time maxima for latched inputs, deferred queue, and runtime nodes
    - _Requirements: 14.7_
  - [x] 14.2 Refactor C runtime context/node storage to fixed arrays
    - Replace heap-backed runtime core buffers with arrays bounded by config macros
    - _Requirements: 2.5, 3.4, 3.6, 4.5, 14.6, 14.7_
  - [x] 14.3 Validate examples build after fixed-array migration
    - Compile FSM/PN examples to confirm compatibility
    - _Requirements: 10.1, 9.1_

- [x] 15. Migrate C node model to embedding + add init/create API pair
  - [x] 15.1 Refactor core runtime node API to typed embedded-node pointers
    - Remove `void* self` adapter pattern and store `rx_node*` in runtime registry
    - _Requirements: 4.1, 12.1, 12.4, 12.5_
  - [x] 15.2 Update FSM/PN structs to embed `rx_node` base
    - Make FSM and PN runtime registration use the embedded base member
    - _Requirements: 12.2, 12.3, 12.4_
  - [x] 15.3 Add `_create`/`_destroy` convenience APIs alongside `_init`
    - Implement create/destroy pairs for runtime/model entities while preserving init APIs
    - _Requirements: 9.1, 9.6, 13.1, 13.2, 13.3, 13.4, 13.5_
  - [x] 15.4 Build-check C examples after API refactor
    - Confirm all C examples still compile and run
    - _Requirements: 10.1, 9.1_

- [x] 16. Migrate canonical C examples to `_create/_destroy` APIs
  - [x] 16.1 Update FSM console example to use create/destroy lifecycle
    - Replace stack+init/free pattern with create/destroy pattern for runtime and machines
    - _Requirements: 10.1, 13.1, 13.2, 13.3_
  - [x] 16.2 Update PN console example to use create/destroy lifecycle
    - Replace stack+init/free pattern with create/destroy pattern for runtime and net
    - _Requirements: 10.1, 13.1, 13.2, 13.3_
  - [x] 16.3 Build-check updated examples
    - Compile and execute both examples after migration
    - _Requirements: 10.1, 9.1_

- [x] 17. Reorder C frontend runtime wrappers for base-runtime upcast
  - [x] 17.1 Place `rx_runtime` as first member in FSM/PN runtime wrappers
    - Enable safe upcast from frontend runtime pointer to base runtime pointer
    - _Requirements: 12.6_
  - [x] 17.2 Build-check C examples after wrapper layout change
    - Ensure no behavioral regressions from struct field reordering
    - _Requirements: 10.1, 9.1_

- [x] 18. Eliminar código muerto y corregir brecha spec/implementación
  - [x] 18.1 Eliminar `RXNET_MAX_INPUT_SIZE` de config.h y specs
    - El macro está definido pero nunca referenciado en el código
    - Eliminar de `config.h`, requirements §2 y design §Context
  - [x] 18.2 Actualizar requirements §2 para reflejar el modelo real de inputs
    - El contexto C solo tiene la cola de diferidos; los inputs son responsabilidad de cada nodo
    - Eliminar referencias a `inputs`/`latched_inputs` en `rx_context`; documentar patrón real
  - [x] 18.3 Actualizar design §Context y §API signatures para coincidir con implementación
    - Corregir firma `rx_context_init(ctx)` (no tiene parámetros de inputs)
    - Corregir firma `rx_fsm_machine_init` (incluye latch/dump callbacks)
    - Eliminar descripción de campos `inputs`/`latched_inputs` en `rx_context`

- [x] 19. Unificar callbacks latch/dump en PN con convenio `(ctx, user)`
  - [x] 19.1 Añadir campos `latch_inputs_fn` y `dump_outputs_fn` a `rx_pn_net`
    - Tipo: `void (*)(rx_pn_context *ctx, void *user)`, igual que FSM
    - La vtable interna sigue usando `(node, ctx)` pero delega a estos campos si están definidos
  - [x] 19.2 Actualizar `rx_pn_net_init` para recibir y registrar ambas callbacks
    - Añadir `latch_inputs_fn` y `dump_outputs_fn` como parámetros opcionales (NULL = noop)
  - [x] 19.3 Migrar ejemplos PN a la nueva API de callbacks
    - Eliminar el patrón de cast manual `(rx_pn_net *)node` en los ejemplos
  - [x] 19.4 Actualizar tests y parity runner tras el cambio de API
  - [x] 19.5 Actualizar requirements y design para documentar el nuevo convenio

- [x] 20. Añadir documentación de patrones de concurrencia
  - [x] 20.1 Añadir sección "Integration Patterns" a requirements §14
    - Tres patrones: ejecutivo cíclico, threads con mutex, RTOS con notificación de tarea
    - Describir qué proteger (escritura de inputs + llamada a tick)
    - Describir cuándo no hace falta mutex (inputs word-sized atómicos en ARM Cortex-M)
  - [x] 20.2 Añadir sección "Concurrency" a design §Host Integration Layer
    - Código de ejemplo para cada patrón (pthread, FreeRTOS, ejecutivo cíclico)
    - Diagrama de secuencia: writer → inputs → latch → tick → outputs

- [x] 21. Añadir ejemplo de runtime mixto FSM + PN
  - [x] 21.1 Implementar `c/examples/mixed/main_cli.c`
    - Un runtime base `rx_runtime` con un `rx_fsm_machine` y un `rx_pn_net` registrados juntos
    - Demuestra que ambos tipos de nodo comparten el mismo tick
  - [x] 21.2 Añadir target `mixed` al Makefile
  - [x] 21.3 Documentar el patrón en design §Host Integration Layer

- [x] 22. Reescribir READMEs con guía real de integración
  - [x] 22.1 Reescribir `c/README.md`
    - Corregir targets de make obsoletos (`pn_00_queue` no existe)
    - Añadir: qué es rxnet, cuándo usar FSM vs PN, patrón básico, los tres modelos de concurrencia
  - [x] 22.2 Reescribir `README.md` raíz
    - Añadir getting-started, tabla FSM vs PN, enlace a ejemplos y specs

## Notes

- Tasks marked with `*` are optional for early MVP but recommended for robustness.
- This file tracks implementation status against current repository state as of April 2026.
- Any behavior change should first update `docs/specs/c/requirements.md`, then `docs/specs/c/design.md`, then this task plan.
