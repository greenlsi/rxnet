from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import Any, Protocol


class Node(Protocol):
    def evaluate(self, ctx: "Context") -> None: ...

    def commit(self, ctx: "Context") -> None: ...


@dataclass(slots=True)
class DeferredAction:
    fn: Any
    user: Any


class Context:
    __slots__ = ("inputs", "latched_inputs", "_deferred_actions")

    def __init__(self, inputs: Any) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)
        self._deferred_actions: list[DeferredAction] = []

    def latch_inputs(self) -> None:
        self.latched_inputs = copy.copy(self.inputs)

    def enqueue_deferred_action(self, fn: Any, user: Any) -> None:
        self._deferred_actions.append(DeferredAction(fn=fn, user=user))

    def run_deferred_actions(self) -> None:
        for action in self._deferred_actions:
            action.fn(self, action.user)
        self._deferred_actions.clear()


class Runtime:
    __slots__ = ("context", "_nodes")

    def __init__(self, inputs: Any) -> None:
        self.context = Context(inputs)
        self._nodes: list[Node] = []

    @property
    def nodes(self) -> list[Node]:
        return self._nodes

    def add_node(self, node: Node) -> None:
        self._nodes.append(node)

    def tick(self) -> None:
        self.context.latch_inputs()

        for node in self._nodes:
            node.evaluate(self.context)

        for node in self._nodes:
            node.commit(self.context)

        self.context.run_deferred_actions()
