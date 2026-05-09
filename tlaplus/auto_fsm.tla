---- MODULE auto_fsm ----
\* Copyright 2026 Jose M. Moya <jm.moya@upm.es>
\* SPDX-License-Identifier: GPL-3.0-or-later
\*
\* GENERATED — do not edit by hand.
\* Source: auto.yaml   (regenerate with: python -m rxnet.tools.gen auto.yaml)
\*
\* Guards are modelled as nondeterministic boolean environment variables.
\* This gives a conservative over-approximation: TLC explores all possible
\* guard sequences.  Liveness properties require explicit fairness constraints.
EXTENDS Integers, TLC

\* ── state identifiers ────────────────────────────────────────────────────
S_OFF == 0
S_ON == 1

States == {S_OFF, S_ON}

\* ── variables ────────────────────────────────────────────────────────────
VARIABLES
    state,   \* current machine state (integer)
    ButtonPressed,   \* abstract guard (boolean environment input)
    AutoOffElapsed,   \* abstract guard (boolean environment input)

vars == <<state, ButtonPressed, AutoOffElapsed>>

\* ── initial state ────────────────────────────────────────────────────────
Init ==
    /\ state = S_OFF
    /\ ButtonPressed \in BOOLEAN
    /\ AutoOffElapsed \in BOOLEAN

\* ── transitions ──────────────────────────────────────────────────────────

\* OFF --[button_pressed]--> ON  (action: auto_on)
T_OFF_ON_button_pressed ==
    /\ state = S_OFF
    /\ ButtonPressed
    /\ state' = S_ON
    /\ ButtonPressed' \in BOOLEAN
    /\ AutoOffElapsed' \in BOOLEAN

\* ON --[button_pressed]--> ON  (action: auto_on)
T_ON_ON_button_pressed ==
    /\ state = S_ON
    /\ ButtonPressed
    /\ state' = S_ON
    /\ ButtonPressed' \in BOOLEAN
    /\ AutoOffElapsed' \in BOOLEAN

\* ON --[auto_off_elapsed]--> OFF  (action: auto_off)
T_ON_OFF_auto_off_elapsed ==
    /\ state = S_ON
    /\ AutoOffElapsed
    /\ state' = S_OFF
    /\ ButtonPressed' \in BOOLEAN
    /\ AutoOffElapsed' \in BOOLEAN

\* allow the system to stutter (required for UNCHANGED semantics)
Stutter == UNCHANGED <<state, ButtonPressed, AutoOffElapsed>>

Next ==
       T_OFF_ON_button_pressed
    \/ T_ON_ON_button_pressed
    \/ T_ON_OFF_auto_off_elapsed
    \/ Stutter

Spec == Init /\ [][Next]_<<state, ButtonPressed, AutoOffElapsed>>

\* ── type invariant ──────────────────────────────────────────────────────
TypeOK ==
    /\ state \in States
    /\ ButtonPressed \in BOOLEAN
    /\ AutoOffElapsed \in BOOLEAN

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
\*   Reaches_ON == <>(state = S_ON)

====
