from __future__ import annotations

from dataclasses import dataclass

from model import Inputs


@dataclass
class SystemDriver:
    sequence: list[tuple[int, int]]
    _index: int = 0

    def write_inputs(self, inputs: Inputs) -> None:
        start, stop = self.sequence[self._index % len(self.sequence)]
        self._index += 1
        inputs.start = start
        inputs.stop = stop
