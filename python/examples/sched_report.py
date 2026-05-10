# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import sys
from typing import Any

from rxnet.runtime import SchedReport, print_sched_report


def report_sched(name: str, executor: Any) -> None:
    report = SchedReport()
    status = executor.check_schedulability(report, sys.stdout)
    print_sched_report(name, status, report)


def make_sched_command(name: str, executor: Any):
    def _cmd(line: str, user: object) -> None:
        report_sched(name, executor)

    return _cmd
