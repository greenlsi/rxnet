from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Optional, Sequence

from .runtime import Context, Runtime as CoreRuntime

Guard = Callable[[Context, Any], bool]
Action = Callable[[Context, Any], None]


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

    @staticmethod
    def _validate_arc(net: "Net", arc: Arc) -> None:
        if arc.place_id < 0 or arc.place_id >= net.place_count:
            raise IndexError(f"place_id out of range: {arc.place_id}")
        if arc.weight < 0:
            raise ValueError("arc weight must be >= 0")

    @classmethod
    def _transition_enabled(cls, net: "Net", transition: Transition) -> bool:
        for arc in transition.consume:
            cls._validate_arc(net, arc)
            if net.places[arc.place_id] < arc.weight:
                return False

        for arc in transition.produce:
            cls._validate_arc(net, arc)

        return True

    @staticmethod
    def _apply_delta(net: "Net", transition: Transition) -> None:
        for arc in transition.consume:
            net._next_places[arc.place_id] -= arc.weight

        for arc in transition.produce:
            net._next_places[arc.place_id] += arc.weight

    def evaluate(self, ctx: Context) -> None:
        self._next_places[:] = self.places
        self._fire_flags[:] = [False] * len(self.transitions)

        for idx, transition in enumerate(self.transitions):
            if not self._transition_enabled(self, transition):
                continue

            if transition.guard is not None and not transition.guard(ctx, self.user):
                continue

            self._fire_flags[idx] = True

    def commit(self, ctx: Context) -> None:
        for should_fire, transition in zip(self._fire_flags, self.transitions):
            if not should_fire:
                continue

            self._apply_delta(self, transition)
            if transition.action is not None:
                ctx.enqueue_deferred_action(transition.action, self.user)

        self.places[:] = self._next_places


class Runtime:
    __slots__ = ("_core",)

    def __init__(self, inputs: Any) -> None:
        self._core = CoreRuntime(inputs)

    @property
    def context(self) -> Context:
        return self._core.context

    @property
    def nets(self) -> Sequence[Net]:
        return [node for node in self._core.nodes if isinstance(node, Net)]

    def add_net(self, net: Net) -> None:
        self._core.add_node(net)

    def tick(self) -> None:
        self._core.tick()
