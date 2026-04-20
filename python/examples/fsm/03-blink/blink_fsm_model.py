# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT
#
# GENERATED — do not edit by hand.
# Source: blink.yaml   (regenerate with: python -m rxnet.tools.gen blink.yaml)

"""Auto-generated model for the ``blink`` FSM.

Exports state constants, a state-name mapping, and the wiring table
consumed by the hand-written implementation file.
"""

# ── states ───────────────────────────────────────────────────────────────
BLINK_STATE_OFF = 0
BLINK_STATE_X1 = 1
BLINK_STATE_X2 = 2

BLINK_STATE_NAMES: dict[int, str] = {
    0: 'OFF',
    1: 'X1',
    2: 'X2',
}

# ── transition wiring table ─────────────────────────────────────────────────
# Each entry: (from_state_id, to_state_id, guard_name | None, action_name | None)
BLINK_TRANSITIONS: list[tuple[int, int, str | None, str | None]] = [
    (BLINK_STATE_OFF, BLINK_STATE_X1, 'button_pressed', 'enter_x1'),  # OFF → X1  [button_pressed]
    (BLINK_STATE_X1, BLINK_STATE_X2, 'button_pressed', 'enter_x2'),  # X1 → X2  [button_pressed]
    (BLINK_STATE_X2, BLINK_STATE_OFF, 'button_pressed', 'enter_off'),  # X2 → OFF  [button_pressed]
    (BLINK_STATE_X1, BLINK_STATE_X1, 'toggle_due', 'toggle_light'),  # X1 → X1  [toggle_due]
    (BLINK_STATE_X2, BLINK_STATE_X2, 'toggle_due', 'toggle_light'),  # X2 → X2  [toggle_due]
]
