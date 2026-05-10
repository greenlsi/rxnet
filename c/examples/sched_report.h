// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdio.h>

#include "rxnet/coop.h"
#include "rxnet/cyclic.h"
#include "rxnet/runtime.h"
#include "rxnet/thread.h"

#if defined(__GNUC__) || defined(__clang__)
#define RX_EXAMPLE_UNUSED __attribute__((unused))
#else
#define RX_EXAMPLE_UNUSED
#endif

static const char *rx_example_sched_status_name(int status) RX_EXAMPLE_UNUSED;
static void rx_example_sched_report_print(const char *name,
                                          int status,
                                          const rx_sched_report *report)
                                          RX_EXAMPLE_UNUSED;

static const char *
rx_example_sched_status_name(int status)
{
    switch (status) {
        case RX_SCHED_SCHEDULABLE:   return "schedulable";
        case RX_SCHED_UNSCHEDULABLE: return "not schedulable";
        case RX_SCHED_UNSUPPORTED:   return "unsupported";
        default:                     return "error";
    }
}

typedef struct {
    const char *name;
    rx_coop_exec *exec;
} rx_example_coop_sched_command;

typedef struct {
    const char *name;
    rx_cyclic_exec *exec;
} rx_example_cyclic_sched_command;

typedef struct {
    const char *name;
    rx_thread_exec *exec;
} rx_example_thread_sched_command;

static void RX_EXAMPLE_UNUSED
rx_example_cmd_coop_sched(rx_fsm_context *ctx, cli_machine_data *cli,
                          const char *command, void *user)
{
    rx_example_coop_sched_command *cmd =
        (rx_example_coop_sched_command *)user;
    rx_sched_report report;
    int status;

    (void)ctx;
    (void)cli;
    (void)command;
    status = rx_coop_exec_check_schedulability(cmd->exec, &report, stdout);
    rx_example_sched_report_print(cmd->name, status, &report);
}

static void RX_EXAMPLE_UNUSED
rx_example_cmd_cyclic_sched(rx_fsm_context *ctx, cli_machine_data *cli,
                            const char *command, void *user)
{
    rx_example_cyclic_sched_command *cmd =
        (rx_example_cyclic_sched_command *)user;
    rx_sched_report report;
    int status;

    (void)ctx;
    (void)cli;
    (void)command;
    status = rx_cyclic_exec_check_schedulability(cmd->exec, &report, stdout);
    rx_example_sched_report_print(cmd->name, status, &report);
}

static void RX_EXAMPLE_UNUSED
rx_example_cmd_thread_sched(rx_fsm_context *ctx, cli_machine_data *cli,
                            const char *command, void *user)
{
    rx_example_thread_sched_command *cmd =
        (rx_example_thread_sched_command *)user;
    rx_sched_report report;
    int status;

    (void)ctx;
    (void)cli;
    (void)command;
    status = rx_thread_exec_check_schedulability(cmd->exec, &report, stdout);
    rx_example_sched_report_print(cmd->name, status, &report);
}

static void
rx_example_sched_report_print(const char *name,
                              int status,
                              const rx_sched_report *report)
{
    int i;

    printf("%s: %s\n", name, rx_example_sched_status_name(status));
    if (report != NULL && report->unsupported) {
        printf("  analysis unsupported for this executor/configuration\n");
    }
    if (report == NULL) return;

    for (i = 0; i < report->task_count; ++i) {
        const rx_sched_task_result *t = &report->tasks[i];
        int r;
        printf("  rt=%p node=%u C=%ldus T=%ldus D=%ldus B=%ldus I=%ldus R=%ldus %s\n",
               (void *)t->rt,
               (unsigned)t->node_idx,
               t->wcet_us,
               t->period_us,
               t->deadline_us,
               t->blocking_us,
               t->interference_us,
               t->response_us,
               t->schedulable ? "OK" : "MISS");
        if (t->resource_access_count > 0) {
            printf("    resources:");
            for (r = 0; r < t->resource_access_count; ++r) {
                printf(" resource=%d max=%ldus",
                       t->resource_accesses[r].resource_id,
                       t->resource_accesses[r].max_us);
            }
            printf("\n");
        }
    }
    if (status == RX_SCHED_ERROR && report->task_count == 0) {
        printf("  no WCET samples yet; let the example run a few ticks first\n");
    }
}

#undef RX_EXAMPLE_UNUSED
