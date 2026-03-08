from __future__ import annotations

import copy
from dataclasses import dataclass, field
from typing import Any, Callable, Optional, Sequence

Guard = Callable[["Context", Any], bool]
Action = Callable[["Context", Any], None]


@dataclass(frozen=True, slots=True)
class Arc:
    place_id: int
    weight: int = 1


@dataclass(frozen=True, slots=True)
class Transition:
    consume: Sequence[Arc] = ()
    produce: Sequence[Arc] = ()
    guard: Optional[Guard] = None
    action: Optional[Action] = None


@dataclass(slots=True)
class Net:
    name: str
    places: Sequence[int]
    transitions: Sequence[Transition]
    user: Any = None
    _next_places: list[int] = field(init=False, repr=False)
    _fire_flags: list[bool] = field(init=False, repr=False)

    def __post_init__(self) -> None:
        self.places = [int(v) for v in self.places]
        self._next_places = list(self.places)
        self._fire_flags = [False] * len(self.transitions)

    @property
    def place_count(self) -> int:
        return len(self.places)


class Context:
    __slots__ = ("inputs", "latched_inputs")

    def __init__(self, inputs: Any) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)

    def _latch(self) -> None:
        self.latched_inputs = copy.copy(self.inputs)


class Runtime:
    __slots__ = ("context", "_nets", "_action_queue")

    def __init__(self, inputs: Any) -> None:
        self.context = Context(inputs)
        self._nets: list[Net] = []
        self._action_queue: list[tuple[Action, Any]] = []

    @property
    def nets(self) -> Sequence[Net]:
        return self._nets

    def add_net(self, net: Net) -> None:
        self._nets.append(net)

    @staticmethod
    def _validate_arc(net: Net, arc: Arc) -> None:
        if arc.place_id < 0 or arc.place_id >= net.place_count:
            raise IndexError(f"place_id out of range: {arc.place_id}")
        if arc.weight < 0:
            raise ValueError("arc weight must be >= 0")

    @classmethod
    def _transition_enabled(cls, net: Net, transition: Transition) -> bool:
        for arc in transition.consume:
            cls._validate_arc(net, arc)
            if net.places[arc.place_id] < arc.weight:
                return False

        for arc in transition.produce:
            cls._validate_arc(net, arc)

        return True

    @staticmethod
    def _apply_delta(net: Net, transition: Transition) -> None:
        for arc in transition.consume:
            net._next_places[arc.place_id] -= arc.weight

        for arc in transition.produce:
            net._next_places[arc.place_id] += arc.weight

    def tick(self) -> None:
        self.context._latch()
        self._action_queue.clear()

        for net in self._nets:
            net._next_places[:] = net.places
            net._fire_flags[:] = [False] * len(net.transitions)

            for idx, transition in enumerate(net.transitions):
                if not self._transition_enabled(net, transition):
                    continue

                if transition.guard is not None and not transition.guard(self.context, net.user):
                    continue

                net._fire_flags[idx] = True

        for net in self._nets:
            for should_fire, transition in zip(net._fire_flags, net.transitions):
                if not should_fire:
                    continue

                self._apply_delta(net, transition)
                if transition.action is not None:
                    self._action_queue.append((transition.action, net.user))

            net.places[:] = net._next_places

        for action, user in self._action_queue:
            action(self.context, user)
