# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT
#
# GENERATED — do not edit by hand.
# Source: auto.yaml   (regenerate with: python -m rxnet.tools.gen auto.yaml)

"""Auto-generated model for the ``auto`` FSM.

Exports state constants, a state-name mapping, and the wiring table
consumed by the hand-written implementation file.
"""

# ── states ───────────────────────────────────────────────────────────────
AUTO_STATE_OFF = 0
AUTO_STATE_ON = 1

AUTO_STATE_NAMES: dict[int, str] = {
    0: 'OFF',
    1: 'ON',
}

# ── transition wiring table ─────────────────────────────────────────────────
# Each entry: (from_state_id, to_state_id, guard_name | None, action_name | None)
AUTO_TRANSITIONS: list[tuple[int, int, str | None, str | None]] = [
    (AUTO_STATE_OFF, AUTO_STATE_ON, 'button_pressed', 'auto_on'),  # OFF → ON  [button_pressed]
    (AUTO_STATE_ON, AUTO_STATE_ON, 'button_pressed', 'auto_on'),  # ON → ON  [button_pressed]
    (AUTO_STATE_ON, AUTO_STATE_OFF, 'auto_off_elapsed', 'auto_off'),  # ON → OFF  [auto_off_elapsed]
]
