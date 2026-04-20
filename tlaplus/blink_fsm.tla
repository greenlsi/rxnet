---- MODULE blink_fsm ----
\* Copyright 2026 Jose M. Moya <jm.moya@upm.es>
\* SPDX-License-Identifier: MIT
\*
\* GENERATED — do not edit by hand.
\* Source: blink.yaml   (regenerate with: python -m rxnet.tools.gen blink.yaml)
\*
\* Guards are modelled as nondeterministic boolean environment variables.
\* This gives a conservative over-approximation: TLC explores all possible
\* guard sequences.  Liveness properties require explicit fairness constraints.
EXTENDS Integers, TLC

\* ── state identifiers ────────────────────────────────────────────────────
S_OFF == 0
S_X1 == 1
S_X2 == 2

States == {S_OFF, S_X1, S_X2}

\* ── variables ────────────────────────────────────────────────────────────
VARIABLES
    state,   \* current machine state (integer)
    ButtonPressed,   \* abstract guard (boolean environment input)
    ToggleDue,   \* abstract guard (boolean environment input)

vars == <<state, ButtonPressed, ToggleDue>>

\* ── initial state ────────────────────────────────────────────────────────
Init ==
    /\ state = S_OFF
    /\ ButtonPressed \in BOOLEAN
    /\ ToggleDue \in BOOLEAN

\* ── transitions ──────────────────────────────────────────────────────────

\* OFF --[button_pressed]--> X1  (action: enter_x1)
T_OFF_X1_button_pressed ==
    /\ state = S_OFF
    /\ ButtonPressed
    /\ state' = S_X1
    /\ ButtonPressed' \in BOOLEAN
    /\ ToggleDue' \in BOOLEAN

\* X1 --[button_pressed]--> X2  (action: enter_x2)
T_X1_X2_button_pressed ==
    /\ state = S_X1
    /\ ButtonPressed
    /\ state' = S_X2
    /\ ButtonPressed' \in BOOLEAN
    /\ ToggleDue' \in BOOLEAN

\* X2 --[button_pressed]--> OFF  (action: enter_off)
T_X2_OFF_button_pressed ==
    /\ state = S_X2
    /\ ButtonPressed
    /\ state' = S_OFF
    /\ ButtonPressed' \in BOOLEAN
    /\ ToggleDue' \in BOOLEAN

\* X1 --[toggle_due]--> X1  (action: toggle_light)
T_X1_X1_toggle_due ==
    /\ state = S_X1
    /\ ToggleDue
    /\ state' = S_X1
    /\ ButtonPressed' \in BOOLEAN
    /\ ToggleDue' \in BOOLEAN

\* X2 --[toggle_due]--> X2  (action: toggle_light)
T_X2_X2_toggle_due ==
    /\ state = S_X2
    /\ ToggleDue
    /\ state' = S_X2
    /\ ButtonPressed' \in BOOLEAN
    /\ ToggleDue' \in BOOLEAN

\* allow the system to stutter (required for UNCHANGED semantics)
Stutter == UNCHANGED <<state, ButtonPressed, ToggleDue>>

Next ==
       T_OFF_X1_button_pressed
    \/ T_X1_X2_button_pressed
    \/ T_X2_OFF_button_pressed
    \/ T_X1_X1_toggle_due
    \/ T_X2_X2_toggle_due
    \/ Stutter

Spec == Init /\ [][Next]_<<state, ButtonPressed, ToggleDue>>

\* ── type invariant ──────────────────────────────────────────────────────
TypeOK ==
    /\ state \in States
    /\ ButtonPressed \in BOOLEAN
    /\ ToggleDue \in BOOLEAN

\* ── properties to verify ────────────────────────────────────────────────
\* Uncomment and adapt for TLC model checking.
\*
\* Safety — machine stays in the defined state space (should always hold):
\*   THEOREM Spec => []TypeOK
\*
\* Liveness — add fairness then check a reachability property:
\*   Fairness == WF_vars(Next)
\*   LiveSpec == Spec /\ Fairness
\*
\*   Reaches_X1 == <>(state = S_X1)
\*   Reaches_X2 == <>(state = S_X2)

====
