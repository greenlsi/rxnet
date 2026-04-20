# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT
#
# GENERATED — do not edit by hand.
# Source: light.yaml   (regenerate with: python -m rxnet.tools.gen light.yaml)

"""Auto-generated model for the ``light`` FSM.

Exports state constants, a state-name mapping, and the wiring table
consumed by the hand-written implementation file.
"""

# ── states ───────────────────────────────────────────────────────────────
LIGHT_STATE_OFF = 0
LIGHT_STATE_ON = 1

LIGHT_STATE_NAMES: dict[int, str] = {
    0: 'OFF',
    1: 'ON',
}

# ── transition wiring table ─────────────────────────────────────────────────
# Each entry: (from_state_id, to_state_id, guard_name | None, action_name | None)
LIGHT_TRANSITIONS: list[tuple[int, int, str | None, str | None]] = [
    (LIGHT_STATE_OFF, LIGHT_STATE_ON, 'button_pressed', 'light_on'),  # OFF → ON  [button_pressed]
    (LIGHT_STATE_ON, LIGHT_STATE_OFF, 'button_pressed', 'light_off'),  # ON → OFF  [button_pressed]
]
