from __future__ import annotations

from dataclasses import dataclass

from rxnet import Context, Machine, Runtime, Transition

START = 0
STOP = 1

IDLE = 0
RUNNING = 1

STATE_NAME = {
    IDLE: "IDLE",
    RUNNING: "RUNNING",
}


@dataclass
class AppData:
    motor_enabled: int = 0
    lamp_enabled: int = 0


def start_pressed(ctx: Context, user: AppData) -> bool:
    return ctx.read_input(START) != 0


def stop_pressed(ctx: Context, user: AppData) -> bool:
    return ctx.read_input(STOP) != 0


def motor_on(ctx: Context, user: AppData) -> None:
    user.motor_enabled = 1


def motor_off(ctx: Context, user: AppData) -> None:
    user.motor_enabled = 0


def lamp_on(ctx: Context, user: AppData) -> None:
    user.lamp_enabled = 1


def lamp_off(ctx: Context, user: AppData) -> None:
    user.lamp_enabled = 0


def print_status(step: str, motor: Machine, lamp: Machine, app: AppData) -> None:
    print(
        f"{step}: "
        f"motor_state={STATE_NAME[motor.state]} "
        f"lamp_state={STATE_NAME[lamp.state]} "
        f"motor_enabled={app.motor_enabled} "
        f"lamp_enabled={app.lamp_enabled}"
    )


def main() -> None:
    app = AppData()
    runtime = Runtime(input_count=2)

    motor = Machine(
        name="motor",
        state=IDLE,
        user=app,
        transitions=[
            Transition(IDLE, RUNNING, start_pressed, motor_on),
            Transition(RUNNING, IDLE, stop_pressed, motor_off),
        ],
    )

    lamp = Machine(
        name="lamp",
        state=IDLE,
        user=app,
        transitions=[
            Transition(IDLE, RUNNING, start_pressed, lamp_on),
            Transition(RUNNING, IDLE, stop_pressed, lamp_off),
        ],
    )

    runtime.add_machine(motor)
    runtime.add_machine(lamp)

    print_status("init", motor, lamp, app)

    runtime.context.stage_input(START, 1)
    runtime.context.stage_input(STOP, 0)
    runtime.tick()
    print_status("after start", motor, lamp, app)

    runtime.context.stage_input(START, 0)
    runtime.context.stage_input(STOP, 1)
    runtime.tick()
    print_status("after stop", motor, lamp, app)


if __name__ == "__main__":
    main()
