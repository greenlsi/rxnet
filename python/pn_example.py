from __future__ import annotations

from dataclasses import dataclass

from rxnet.pn import Arc, Context, Net, Runtime, Transition

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


def main() -> None:
    app = AppData()
    runtime = Runtime(inputs=Inputs())

    net = Net(
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

    runtime.add_net(net)

    print(f"init: places={net.places} fired={app.fired_count}")

    runtime.context.inputs.add = 1
    runtime.context.inputs.remove = 0
    runtime.tick()
    print(f"after add: places={net.places} fired={app.fired_count}")

    runtime.context.inputs.add = 0
    runtime.context.inputs.remove = 1
    runtime.tick()
    print(f"after remove: places={net.places} fired={app.fired_count}")


if __name__ == "__main__":
    main()
