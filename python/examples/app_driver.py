"""Simulated GPIO driver — host-side mock for interactive CLI examples.

Mirrors the API of the C ``app_driver.h`` / ``app_driver.c`` (host-mock
section).  All state is module-level so that multiple machines that share
a GPIO number see the same event flags, just like the embedded version.
"""
from __future__ import annotations

_button_registered: dict[int, bool] = {}
_button_event: dict[int, bool] = {}
_light_initialized: dict[int, bool] = {}
_light_level: dict[int, int] = {}


def init_button(gpio: int) -> None:
    if not _button_registered.get(gpio, False):
        _button_registered[gpio] = True
        _button_event[gpio] = False


def init_light(gpio: int) -> None:
    _light_initialized[gpio] = True
    _light_level[gpio] = 0
    print(f"[app_driver] init light GPIO {gpio} (host mock)")


def trigger_button(gpio: int) -> bool:
    if not _button_registered.get(gpio, False):
        return False
    _button_event[gpio] = True
    return True


def latch_button_event(gpio: int) -> bool:
    return _button_registered.get(gpio, False) and _button_event.get(gpio, False)


def clear_button_event(gpio: int) -> None:
    if _button_registered.get(gpio, False):
        _button_event[gpio] = False


def set_light(gpio: int, enabled: bool) -> None:
    level = 1 if enabled else 0
    if _light_initialized.get(gpio, False) and _light_level.get(gpio) == level:
        return
    _light_initialized[gpio] = True
    _light_level[gpio] = level
    print(f"[app_driver] light GPIO {gpio} -> {'ON' if level else 'OFF'}")
