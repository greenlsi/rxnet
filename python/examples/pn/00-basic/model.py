# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: MIT

from __future__ import annotations

from dataclasses import dataclass

from rxnet.pn import Arc, Context, Net, Transition

P_IDLE = 0
P_BUSY = 1


@dataclass
class Inputs:
    add: int = 0
    remove: int = 0


@dataclass
class AppData:
    fired_count: int = 0


def want_add(ctx: Context, user: AppData) -> bool:
    return ctx.latched_inputs.add != 0


def want_remove(ctx: Context, user: AppData) -> bool:
    return ctx.latched_inputs.remove != 0


def on_fire(ctx: Context, user: AppData) -> None:
    user.fired_count += 1


def build_net(app: AppData) -> Net:
    return Net(
        name="queue",
        places=[1, 0],
        transitions=[
            Transition(
                consume=[Arc(P_IDLE, 1)],
                produce=[Arc(P_BUSY, 1)],
                guard=want_add,
                action=on_fire,
            ),
            Transition(
                consume=[Arc(P_BUSY, 1)],
                produce=[Arc(P_IDLE, 1)],
                guard=want_remove,
                action=on_fire,
            ),
        ],
        user=app,
    )
