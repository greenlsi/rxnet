# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from rxnet.tools.extract_c import (
    _collect_enum_constants,
    _find_machine_init,
    _find_transitions,
    ast_to_spec,
    generate_dot,
    generate_mermaid,
)

LIGHT_AST: dict = {
    "kind": "TranslationUnitDecl",
    "inner": [
        {
            "kind": "EnumDecl",
            "inner": [
                {
                    "kind": "EnumConstantDecl",
                    "name": "LIGHT_STATE_OFF",
                    "inner": [{"kind": "ConstantExpr", "value": 0}],
                },
                {
                    "kind": "EnumConstantDecl",
                    "name": "LIGHT_STATE_ON",
                    "inner": [{"kind": "ConstantExpr", "value": 1}],
                },
            ],
        },
        {
            "kind": "VarDecl",
            "name": "transitions",
            "type": {"qualType": "const rx_fsm_transition[2]"},
            "inner": [
                {
                    "kind": "InitListExpr",
                    "inner": [
                        {
                            "kind": "InitListExpr",
                            "inner": [
                                {"kind": "DeclRefExpr", "referencedDecl": {"name": "LIGHT_STATE_OFF"}},
                                {"kind": "DeclRefExpr", "referencedDecl": {"name": "LIGHT_STATE_ON"}},
                                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "button_pressed"}}]},
                                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "light_on"}}]},
                            ],
                        },
                        {
                            "kind": "InitListExpr",
                            "inner": [
                                {"kind": "DeclRefExpr", "referencedDecl": {"name": "LIGHT_STATE_ON"}},
                                {"kind": "DeclRefExpr", "referencedDecl": {"name": "LIGHT_STATE_OFF"}},
                                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "button_pressed"}}]},
                                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "light_off"}}]},
                            ],
                        },
                    ],
                }
            ],
        },
        {
            "kind": "CallExpr",
            "inner": [
                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "rx_fsm_machine_init"}}]},
                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "machine"}}]},
                {"kind": "ImplicitCastExpr", "inner": [{"kind": "StringLiteral", "value": '"light"'}]},
                {"kind": "DeclRefExpr", "referencedDecl": {"name": "LIGHT_STATE_OFF"}},
                {"kind": "ImplicitCastExpr", "inner": [{"kind": "DeclRefExpr", "referencedDecl": {"name": "transitions"}}]},
            ],
        },
    ],
}


def test_collect_enum_constants():
    result = _collect_enum_constants(LIGHT_AST)
    assert result == {"LIGHT_STATE_OFF": 0, "LIGHT_STATE_ON": 1}


def test_find_transitions():
    pairs = _find_transitions(LIGHT_AST)
    assert pairs == [
        ("LIGHT_STATE_OFF", "LIGHT_STATE_ON", "button_pressed", "light_on"),
        ("LIGHT_STATE_ON", "LIGHT_STATE_OFF", "button_pressed", "light_off"),
    ]


def test_find_machine_init():
    result = _find_machine_init(LIGHT_AST)
    assert result == ("light", "LIGHT_STATE_OFF")
