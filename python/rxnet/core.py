from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, List, Optional, Sequence, Any

Guard = Callable[["Context", Any], bool]
Action = Callable[["Context", Any], None]


@dataclass(frozen=True, slots=True)
class Transition:
    from_state: int
    to_state: int
    guard: Optional[Guard] = None
    action: Optional[Action] = None


@dataclass(slots=True)
class Machine:
    name: str
    state: int
    transitions: Sequence[Transition]
    user: Any = None
    _next_state: int = field(init=False, repr=False)
    _deferred_action: Optional[Action] = field(init=False, default=None, repr=False)

    def __post_init__(self) -> None:
        self._next_state = self.state


class Context:
    __slots__ = ("_staged_inputs", "inputs")

    def __init__(self, input_count: int) -> None:
        if input_count < 0:
            raise ValueError("input_count must be >= 0")
        self._staged_inputs: List[int] = [0] * input_count
        self.inputs: List[int] = [0] * input_count

    @property
    def input_count(self) -> int:
        return len(self.inputs)

    def stage_input(self, input_id: int, value: int) -> None:
        self._staged_inputs[input_id] = int(value)

    def stage_inputs(self, values: Sequence[int]) -> None:
        if len(values) != len(self._staged_inputs):
            raise ValueError("wrong number of inputs")
        self._staged_inputs[:] = [int(value) for value in values]

    def read_input(self, input_id: int) -> int:
        return self.inputs[input_id]

    def _latch(self) -> None:
        self.inputs[:] = self._staged_inputs


class Runtime:
    __slots__ = ("context", "_machines", "_action_queue")

    def __init__(self, input_count: int) -> None:
        self.context = Context(input_count)
        self._machines: List[Machine] = []
        self._action_queue: List[tuple[Action, Any]] = []

    @property
    def machines(self) -> Sequence[Machine]:
        return self._machines

    def add_machine(self, machine: Machine) -> None:
        self._machines.append(machine)

    def tick(self) -> None:
        self.context._latch()
        self._action_queue.clear()

        for machine in self._machines:
            machine._next_state = machine.state
            machine._deferred_action = None

            for transition in machine.transitions:
                if transition.from_state != machine.state:
                    continue

                if transition.guard is None or transition.guard(self.context, machine.user):
                    machine._next_state = transition.to_state
                    machine._deferred_action = transition.action
                    break

        for machine in self._machines:
            machine.state = machine._next_state
            if machine._deferred_action is not None:
                self._action_queue.append((machine._deferred_action, machine.user))

        for action, user in self._action_queue:
            action(self.context, user)
