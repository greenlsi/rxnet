from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Optional, Sequence

from .runtime import Context, Runtime as CoreRuntime

Guard = Callable[[Context, Any], bool]
Action = Callable[[Context, Any], None]
NodePhaseCb = Callable[[Context, Any], None]


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
    latch_inputs_cb: Optional[NodePhaseCb] = None
    dump_outputs_cb: Optional[NodePhaseCb] = None
    _next_state: int = field(init=False, repr=False)
    _proposed_action: Optional[Action] = field(init=False, default=None, repr=False)

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

    def add_machine(self, machine: Machine) -> None:
        self._core.add_node(machine)

    def tick(self) -> None:
        self._core.tick()
