"""Non-blocking CLI helper for the interactive examples.

A background thread reads lines from stdin.  Each call to ``Cli.tick()``
drains one pending line (if any) and dispatches to the matching handler.
Commands are matched by prefix: registering "timeout" matches the line
"timeout a 3000", and the handler receives the full line so it can parse
its own arguments.
"""
from __future__ import annotations

import queue
import sys
import threading
from typing import Any, Callable

CommandHandler = Callable[[str, Any], None]


class Cli:
    """Interactive command-line helper for the tick-based examples."""

    def __init__(self) -> None:
        self._q: queue.SimpleQueue[str] = queue.SimpleQueue()
        self._commands: list[tuple[str, CommandHandler, Any]] = []
        self._extra_help: list[str] = []
        thread = threading.Thread(target=self._reader, daemon=True)
        thread.start()

    # ------------------------------------------------------------------ #
    # Registration                                                         #
    # ------------------------------------------------------------------ #

    def register(self, name: str, handler: CommandHandler, user: Any = None) -> None:
        self._commands.append((name, handler, user))

    def add_help_line(self, line: str) -> None:
        """Register an extra help line for commands with arguments."""
        self._extra_help.append(line)

    # ------------------------------------------------------------------ #
    # Output helpers                                                       #
    # ------------------------------------------------------------------ #

    def print_help(self) -> None:
        print("Commands:")
        for name, _, _ in self._commands:
            print(f"  {name}")
        for line in self._extra_help:
            print(f"  {line}")

    def print_prompt(self) -> None:
        print("> ", end="", flush=True)

    # ------------------------------------------------------------------ #
    # Tick                                                                  #
    # ------------------------------------------------------------------ #

    def tick(self) -> None:
        """Process one pending command if available."""
        try:
            line = self._q.get_nowait()
        except queue.Empty:
            return

        line = line.strip()
        if not line:
            self.print_prompt()
            return

        for name, handler, user in self._commands:
            if line == name or line.startswith(name + " "):
                handler(line, user)
                self.print_prompt()
                return

        print(f"unknown command: {line}")
        self.print_prompt()

    # ------------------------------------------------------------------ #
    # Background reader                                                    #
    # ------------------------------------------------------------------ #

    def _reader(self) -> None:
        try:
            for line in sys.stdin:
                self._q.put(line)
        except (EOFError, OSError):
            pass
