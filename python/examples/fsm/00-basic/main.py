# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from rxnet.fsm import Runtime

from model import AppData, Inputs, build_machines
from system import SystemDriver

STATE_NAME = {
    0: "IDLE",
    1: "RUNNING",
}


def print_status(step: str, motor, lamp, app: AppData) -> None:
    print(
        f"{step}: "
        f"motor_state={STATE_NAME[motor.state]} "
        f"lamp_state={STATE_NAME[lamp.state]} "
        f"motor_enabled={app.motor_enabled} "
        f"lamp_enabled={app.lamp_enabled}"
    )


def main() -> None:
    app = AppData()
    runtime = Runtime(inputs=Inputs())
    driver = SystemDriver(sequence=[(1, 0), (0, 1)])

    motor, lamp = build_machines(app)
    runtime.add_machine(motor)
    runtime.add_machine(lamp)

    print_status("init", motor, lamp, app)

    driver.write_inputs(runtime.context.inputs)
    runtime.tick()
    print_status("after start", motor, lamp, app)

    driver.write_inputs(runtime.context.inputs)
    runtime.tick()
    print_status("after stop", motor, lamp, app)


if __name__ == "__main__":
    main()
