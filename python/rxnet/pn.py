from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from typing import Any

from .runtime import Context
from .runtime import Runtime as CoreRuntime

Guard = Callable[[Context, Any], bool]
Action = Callable[[Context, Any], None]
NodePhaseCb = Callable[[Context, Any], None]


@dataclass(frozen=True, slots=True)
class Arc:
    place_id: int
    weight: int = 1


@dataclass(frozen=True, slots=True)
class Transition:
    consume: Sequence[Arc] = ()
    produce: Sequence[Arc] = ()
    guard: Guard | None = None
    action: Action | None = None


@dataclass(slots=True)
class Net:
    name: str
    places: list[int]
    transitions: Sequence[Transition]
    user: Any = None
    latch_inputs_cb: NodePhaseCb | None = None
    dump_outputs_cb: NodePhaseCb | None = None
    _next_places: list[int] = field(init=False, repr=False)
    _fire_flags: list[bool] = field(init=False, repr=False)

    def __post_init__(self) -> None:
        self.places = [int(v) for v in self.places]
        self._next_places = list(self.places)
        self._fire_flags = [False] * len(self.transitions)
        self._validate_all()

    def _validate_all(self) -> None:
        for transition in self.transitions:
            for arc in transition.consume:
                self._validate_arc(arc)
            for arc in transition.produce:
                self._validate_arc(arc)

    def _validate_arc(self, arc: Arc) -> None:
        if arc.place_id < 0 or arc.place_id >= len(self.places):
            raise IndexError(f"place_id out of range: {arc.place_id}")
        if arc.weight < 0:
            raise ValueError("arc weight must be >= 0")

    @property
    def place_count(self) -> int:
        return len(self.places)

    def _is_enabled(self, transition: Transition) -> bool:
        """Check against _next_places (greedy sequential: earlier fires consume tokens)."""
        for arc in transition.consume:
            if self._next_places[arc.place_id] < arc.weight:
                return False
        return True

    def _apply_delta(self, transition: Transition) -> None:
        for arc in transition.consume:
            self._next_places[arc.place_id] -= arc.weight
        for arc in transition.produce:
            self._next_places[arc.place_id] += arc.weight

    def latch_inputs(self, ctx: Context) -> None:
        if self.latch_inputs_cb is not None:
            self.latch_inputs_cb(ctx, self.user)

    def evaluate(self, ctx: Context) -> None:
        self._next_places[:] = self.places
        self._fire_flags[:] = [False] * len(self.transitions)

        for idx, transition in enumerate(self.transitions):
            if not self._is_enabled(transition):
                continue

            if transition.guard is not None and not transition.guard(ctx, self.user):
                continue

            self._fire_flags[idx] = True
            # Apply delta immediately so subsequent transitions see updated token
            # counts — greedy sequential / first-match-wins semantics.
            self._apply_delta(transition)

    def commit(self, ctx: Context) -> None:
        # Deltas were applied to _next_places during evaluate; commit the result.
        self.places[:] = self._next_places

        for should_fire, transition in zip(self._fire_flags, self.transitions):
            if not should_fire:
                continue
            if transition.action is not None:
                ctx.enqueue_deferred_action(transition.action, self.user)

    def dump_outputs(self, ctx: Context) -> None:
        if self.dump_outputs_cb is not None:
            self.dump_outputs_cb(ctx, self.user)


class Runtime:
    __slots__ = ("_core",)

    def __init__(self, inputs: Any = None) -> None:
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
