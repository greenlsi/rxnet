# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

from __future__ import annotations

from dataclasses import dataclass

from rxnet.fsm import Context, Machine, Transition

IDLE = 0
RUNNING = 1


@dataclass
class Inputs:
    start: int = 0
    stop: int = 0


@dataclass
class AppData:
    motor_enabled: int = 0
    lamp_enabled: int = 0


def start_pressed(ctx: Context, user: AppData) -> bool:
    return ctx.latched_inputs.start != 0


def stop_pressed(ctx: Context, user: AppData) -> bool:
    return ctx.latched_inputs.stop != 0


def motor_on(ctx: Context, user: AppData) -> None:
    user.motor_enabled = 1


def motor_off(ctx: Context, user: AppData) -> None:
    user.motor_enabled = 0


def lamp_on(ctx: Context, user: AppData) -> None:
    user.lamp_enabled = 1


def lamp_off(ctx: Context, user: AppData) -> None:
    user.lamp_enabled = 0


def build_machines(app: AppData) -> tuple[Machine, Machine]:
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

    return motor, lamp
