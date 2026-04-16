from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import Any, Protocol


class Node(Protocol):
    def latch_inputs(self, ctx: Context) -> None: ...
    def evaluate(self, ctx: Context) -> None: ...
    def commit(self, ctx: Context) -> None: ...
    def dump_outputs(self, ctx: Context) -> None: ...


@dataclass(slots=True)
class DeferredAction:
    fn: Any
    user: Any


class Context:
    __slots__ = ("inputs", "latched_inputs", "_deferred_actions")

    def __init__(self, inputs: Any = None) -> None:
        self.inputs = inputs
        self.latched_inputs = copy.copy(inputs)
        self._deferred_actions: list[DeferredAction] = []

    def latch_inputs(self) -> None:
        self.latched_inputs = copy.copy(self.inputs)

    def enqueue_deferred_action(self, fn: Any, user: Any) -> None:
        if fn is None:
            raise TypeError("fn must not be None")
        self._deferred_actions.append(DeferredAction(fn=fn, user=user))

    def run_deferred_actions(self) -> None:
        for action in self._deferred_actions:
            action.fn(self, action.user)
        self._deferred_actions.clear()


class Runtime:
    __slots__ = ("context", "_nodes")

    def __init__(self, inputs: Any = None) -> None:
        self.context = Context(inputs)
        self._nodes: list[Node] = []

    @property
    def nodes(self) -> list[Node]:
        return self._nodes

    def add_node(self, node: Node) -> None:
        self._nodes.append(node)

    def tick(self) -> None:
        # Phase 1: latch global inputs snapshot, then per-node input latching
        self.context.latch_inputs()
        for node in self._nodes:
            node.latch_inputs(self.context)

        # Phase 2: evaluate all nodes
        for node in self._nodes:
            node.evaluate(self.context)

        # Phase 3: commit all nodes
        for node in self._nodes:
            node.commit(self.context)

        # Phase 4: run deferred actions
        self.context.run_deferred_actions()

        # Phase 5: dump outputs
        for node in self._nodes:
            node.dump_outputs(self.context)
