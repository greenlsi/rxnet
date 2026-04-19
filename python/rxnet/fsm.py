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
class Transition:
    from_state: int
    to_state: int
    guard: Guard | None = None
    action: Action | None = None
    label: str | None = None  # arc label for diagrams and traces


@dataclass(slots=True)
class Machine:
    name: str
    state: int
    transitions: Sequence[Transition]
    user: Any = None
    latch_inputs_cb: NodePhaseCb | None = None
    dump_outputs_cb: NodePhaseCb | None = None
    state_names: dict[int, str] | None = None  # {0: "OFF", 1: "ON"}
    _next_state: int = field(init=False, repr=False)
    _proposed_action: Action | None = field(init=False, default=None, repr=False)

    def __post_init__(self) -> None:
        self._next_state = self.state

    def latch_inputs(self, ctx: Context) -> None:
        if self.latch_inputs_cb is not None:
            self.latch_inputs_cb(ctx, self.user)

    def evaluate(self, ctx: Context) -> None:
        self._next_state = self.state
        self._proposed_action = None

        for transition in self.transitions:
            if transition.from_state != self.state:
                continue

            if transition.guard is None or transition.guard(ctx, self.user):
                self._next_state = transition.to_state
                self._proposed_action = transition.action
                break

    def commit(self, ctx: Context) -> None:
        self.state = self._next_state
        if self._proposed_action is not None:
            ctx.enqueue_deferred_action(self._proposed_action, self.user)

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
    def machines(self) -> Sequence[Machine]:
        return [node for node in self._core.nodes if isinstance(node, Machine)]

    def add_machine(self, machine: Machine, period_us: int = 0) -> None:
        self._core.add_node(machine, period_us)

    def add_node(self, node: Any, period_us: int = 0) -> None:
        """Register a non-Machine node (e.g. a CliNode wrapper)."""
        self._core.add_node(node, period_us)

    def build(self) -> None:
        self._core.build()

    @property
    def period_us(self) -> int:
        return self._core.period_us

    @property
    def nslots(self) -> int:
        return self._core.nslots

    def tick(self) -> None:
        self._core.tick()
