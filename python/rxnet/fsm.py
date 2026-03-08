from __future__ import annotations

import copy
from dataclasses import dataclass, field
from typing import Any, Callable, Optional, Sequence

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
    __slots__ = ("inputs", "latched_inputs")

    def __init__(self, inputs: Any) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)

    def _latch(self) -> None:
        self.latched_inputs = copy.copy(self.inputs)


class Runtime:
    __slots__ = ("context", "_machines", "_action_queue")

    def __init__(self, inputs: Any) -> None:
        self.context = Context(inputs)
        self._machines: list[Machine] = []
        self._action_queue: list[tuple[Action, Any]] = []

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
