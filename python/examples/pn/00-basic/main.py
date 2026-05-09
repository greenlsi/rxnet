# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from rxnet.pn import Runtime

from model import AppData, Inputs, build_net
from system import SystemDriver


def main() -> None:
    app = AppData()
    runtime = Runtime(inputs=Inputs())
    driver = SystemDriver(sequence=[(1, 0), (0, 1)])

    net = build_net(app)
    runtime.add_net(net)

    print(f"init: places={net.places} fired={app.fired_count}")

    driver.write_inputs(runtime.context.inputs)
    runtime.tick()
    print(f"after add: places={net.places} fired={app.fired_count}")

    driver.write_inputs(runtime.context.inputs)
    runtime.tick()
    print(f"after remove: places={net.places} fired={app.fired_count}")


if __name__ == "__main__":
    main()
