// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "rxnet/coop.h"

void
rx_coop_exec_init(rx_coop_exec *ce)
{
    memset(ce, 0, sizeof(*ce));
}

int
rx_coop_exec_add(rx_coop_exec *ce, rx_runtime *rt)
{
    if (ce->ntasks >= (int)RXNET_CE_MAX_TASKS) return -1;

    if (rx_runtime_build(rt) != 0) return -1;

    if (rt->period_us == 0) {
        fprintf(stderr, "rxnet: coop_exec_add: runtime has no periodic nodes "
                "(all async); cannot determine call rate\n");
        return -1;
    }

    ce->tasks[ce->ntasks].rt = rt;
    ++ce->ntasks;
    return 0;
}

static long
effective_deadline(const rx_node_entry *entry)
{
    return entry->deadline_us > 0 ? entry->deadline_us : entry->period_us;
}

static void
sort_active_by_deadline(unsigned char *active, int count, const rx_node_entry *nodes)
{
    int i, j;
    unsigned char tmp;
    long dl_i, dl_j;

    for (i = 1; i < count; ++i) {
        tmp = active[i];
        dl_i = effective_deadline(&nodes[tmp]);
        j = i;
        while (j > 0) {
            dl_j = effective_deadline(&nodes[active[j - 1]]);
            if (dl_j <= dl_i) break;
            active[j] = active[j - 1];
            --j;
        }
        active[j] = tmp;
    }
}

int
rx_coop_exec_enable_sched_check(rx_coop_exec *ce, int enabled)
{
    if (ce == NULL) return -1;
    ce->sched_check_enabled = enabled ? 1 : 0;
    return 0;
}

typedef struct {
    rx_runtime *rt;
    unsigned char node_idx;
} coop_sched_task_ref;

static void
sched_report_reset(rx_sched_report *report)
{
    if (report == NULL) return;
    memset(report, 0, sizeof(*report));
    report->schedulable = 1;
}

static int
task_priority_less(const coop_sched_task_ref *a, const coop_sched_task_ref *b)
{
    long da = effective_deadline(&a->rt->nodes[a->node_idx]);
    long db = effective_deadline(&b->rt->nodes[b->node_idx]);
    if (da != db) return da < db;
    if (a->rt != b->rt) return (uintptr_t)a->rt < (uintptr_t)b->rt;
    return a->node_idx < b->node_idx;
}

static void
sort_tasks_by_priority(coop_sched_task_ref *tasks, int count)
{
    int i, j;
    coop_sched_task_ref tmp;

    for (i = 1; i < count; ++i) {
        tmp = tasks[i];
        j = i;
        while (j > 0 && task_priority_less(&tmp, &tasks[j - 1])) {
            tasks[j] = tasks[j - 1];
            --j;
        }
        tasks[j] = tmp;
    }
}

static long
ceil_div_long(long a, long b)
{
    return (a + b - 1) / b;
}

int
rx_coop_exec_check_schedulability(rx_coop_exec *ce,
                                  rx_sched_report *report,
                                  FILE *log)
{
    coop_sched_task_ref tasks[RXNET_SCHED_MAX_TASKS];
    int count = 0;
    int i, j;
    int all_schedulable = 1;
    size_t n;

    if (ce == NULL) return RX_SCHED_ERROR;
    sched_report_reset(report);

    for (i = 0; i < ce->ntasks; ++i) {
        rx_runtime *rt = ce->tasks[i].rt;
        for (n = 0; n < rt->node_count; ++n) {
            if (rt->nodes[n].period_us <= 0) continue;
            if (count >= (int)RXNET_SCHED_MAX_TASKS) return RX_SCHED_ERROR;
            if (rt->nodes[n].wcet_us <= 0) {
                if (log) fprintf(log, "coop: missing WCET for node %u\n", (unsigned)n);
                return RX_SCHED_ERROR;
            }
            tasks[count].rt = rt;
            tasks[count].node_idx = (unsigned char)n;
            count++;
        }
    }

    sort_tasks_by_priority(tasks, count);
    if (log) fprintf(log, "coop schedulability: tasks=%d\n", count);

    for (i = 0; i < count; ++i) {
        rx_node_entry *ei = &tasks[i].rt->nodes[tasks[i].node_idx];
        long ci = ei->wcet_us;
        long di = effective_deadline(ei);
        long bi = 0;
        long ri_prev, ri;
        int converged = 0;
        rx_sched_task_result *tr = NULL;

        for (j = i + 1; j < count; ++j) {
            long cj = tasks[j].rt->nodes[tasks[j].node_idx].wcet_us;
            if (cj > bi) bi = cj;
        }

        ri_prev = ci + bi;
        for (;;) {
            long interference = 0;
            for (j = 0; j < i; ++j) {
                rx_node_entry *ej = &tasks[j].rt->nodes[tasks[j].node_idx];
                interference += ceil_div_long(ri_prev, ej->period_us) * ej->wcet_us;
            }
            ri = ci + bi + interference;
            if (ri == ri_prev) {
                converged = 1;
                break;
            }
            if (ri > di) {
                break;
            }
            ri_prev = ri;
        }

        if (report != NULL && report->task_count < (int)RXNET_SCHED_MAX_TASKS) {
            tr = &report->tasks[report->task_count++];
            tr->rt = tasks[i].rt;
            tr->node_idx = tasks[i].node_idx;
            tr->period_us = ei->period_us;
            tr->deadline_us = di;
            tr->wcet_us = ci;
            tr->blocking_us = bi;
            tr->response_us = ri;
            tr->interference_us = ri - ci - bi;
            tr->schedulable = converged && ri <= di;
        }
        if (!(converged && ri <= di)) {
            all_schedulable = 0;
            if (report != NULL) report->schedulable = 0;
        }
        if (log) {
            fprintf(log, "node=%u C=%ld T=%ld D=%ld B=%ld I=%ld R=%ld %s\n",
                    (unsigned)tasks[i].node_idx, ci, ei->period_us, di, bi,
                    ri - ci - bi, ri, (converged && ri <= di) ? "OK" : "MISS");
        }
    }

    return all_schedulable ? RX_SCHED_SCHEDULABLE : RX_SCHED_UNSCHEDULABLE;
}

void
rx_coop_exec_run(rx_coop_exec *ce)
{
    rx_tick_t now, nearest;
    int i;
    size_t n;

    if (ce->ntasks == 0) {
        return;
    }

    if (ce->sched_check_enabled &&
        rx_coop_exec_check_schedulability(ce, NULL, NULL) == RX_SCHED_UNSCHEDULABLE) {
        return;
    }

    /* Initialise all activations to "now" so every periodic node fires first. */
    now = rx_tick_now();
    for (i = 0; i < ce->ntasks; ++i) {
        rx_runtime *rt = ce->tasks[i].rt;
        for (n = 0; n < rt->node_count; ++n) {
            ce->tasks[i].next_tick[n] = now;
        }
    }
    ce->stop_requested = 0;

    while (!ce->stop_requested) {
        now = rx_tick_now();

        /* Run every active node whose activation time has passed. */
        for (i = 0; i < ce->ntasks; ++i) {
            rx_runtime *rt = ce->tasks[i].rt;
            unsigned char active[RXNET_MAX_RUNTIME_NODES];
            int count = 0;

            for (n = 0; n < rt->node_count; ++n) {
                long p = rt->nodes[n].period_us;
                if (p > 0 && rx_tick_compare(now, ce->tasks[i].next_tick[n]) >= 0) {
                    active[count++] = (unsigned char)n;
                    /* Advance by one period to track drift rather than resetting
                     * from now, preventing accumulated phase slippage. */
                    ce->tasks[i].next_tick[n] =
                        rx_tick_add_us(ce->tasks[i].next_tick[n], p);
                }
            }

            sort_active_by_deadline(active, count, rt->nodes);

            if (count > 0) {
                for (n = 0; n < rt->node_count; ++n) {
                    if (rt->nodes[n].period_us == 0) {
                        active[count++] = (unsigned char)n;
                    }
                }
                rx_runtime_tick_nodes(rt, active, count);
            }
            if (ce->stop_requested) {
                break;
            }
        }

        /* Sleep until the nearest upcoming deadline. */
        {
            int has_nearest = 0;
            nearest = now;
            for (i = 0; i < ce->ntasks; ++i) {
                rx_runtime *rt = ce->tasks[i].rt;
                for (n = 0; n < rt->node_count; ++n) {
                    if (rt->nodes[n].period_us <= 0) continue;
                    if (!has_nearest ||
                        rx_tick_compare(ce->tasks[i].next_tick[n], nearest) < 0) {
                        nearest = ce->tasks[i].next_tick[n];
                        has_nearest = 1;
                    }
                }
            }
        }
        if (!ce->stop_requested)
            rx_tick_sleep_until(nearest);
    }

    if (ce->on_stop)
        ce->on_stop(ce->on_stop_user);
}

void
rx_coop_exec_stop(rx_coop_exec *ce)
{
    ce->stop_requested = 1;
}

void
rx_coop_exec_on_stop(rx_coop_exec *ce,
                     void (*callback)(void *user),
                     void *user)
{
    ce->on_stop = callback;
    ce->on_stop_user = user;
}
