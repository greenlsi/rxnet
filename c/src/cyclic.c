// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "rxnet/cyclic.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static long
gcd(long a, long b)
{
    while (b) { long t = b; b = a % b; a = t; }
    return a;
}

static long
lcm(long a, long b)
{
    return a / gcd(a, b) * b;
}

/* ------------------------------------------------------------------ */
/* Cyclic executive                                                     */
/* ------------------------------------------------------------------ */

void
rx_cyclic_exec_init(rx_cyclic_exec *ce)
{
    memset(ce, 0, sizeof(*ce));
}

int
rx_cyclic_exec_add(rx_cyclic_exec *ce, rx_runtime *rt)
{
    if (ce->ntasks >= (int)RXNET_CE_MAX_TASKS) return -1;

    if (rx_runtime_build(rt) != 0) return -1;

    if (rt->period_us == 0) {
        fprintf(stderr, "rxnet: cyclic_exec_add: runtime has no periodic nodes "
                "(all async); cannot determine call rate\n");
        return -1;
    }

    ce->tasks[ce->ntasks].rt = rt;
    ++ce->ntasks;
    ce->nslots = 0;
    return 0;
}

static long
effective_deadline(const rx_node_entry *entry)
{
    return entry->deadline_us > 0 ? entry->deadline_us : entry->period_us;
}

static void
sort_slot_by_deadline(rx_ce_slot *slot)
{
    int i, j;
    rx_ce_activation tmp;
    long dl_i, dl_j;

    for (i = 1; i < slot->count; ++i) {
        tmp  = slot->activation[i];
        dl_i = effective_deadline(&tmp.rt->nodes[tmp.node_idx]);
        j = i;
        while (j > 0) {
            rx_ce_activation prev = slot->activation[j - 1];
            dl_j = effective_deadline(&prev.rt->nodes[prev.node_idx]);
            if (dl_j <= dl_i) break;
            slot->activation[j] = slot->activation[j - 1];
            --j;
        }
        slot->activation[j] = tmp;
    }
}

int
rx_cyclic_exec_build(rx_cyclic_exec *ce)
{
    long base_us, hyper_us;
    int  nslots, s, t;
    size_t i;

    if (ce->ntasks == 0) return -1;

    base_us = 0;
    hyper_us = 0;
    for (t = 0; t < ce->ntasks; ++t) {
        rx_runtime *rt = ce->tasks[t].rt;
        for (i = 0; i < rt->node_count; ++i) {
            long p = rt->nodes[i].period_us;
            if (p <= 0) continue;
            if (base_us == 0) {
                base_us = p;
                hyper_us = p;
            } else {
                base_us = gcd(base_us, p);
                hyper_us = lcm(hyper_us, p);
            }
        }
    }
    if (base_us == 0) return -1;

    nslots = (int)(hyper_us / base_us);
    if (nslots > (int)RXNET_CE_MAX_SLOTS) return -1;

    for (s = 0; s < nslots; ++s) {
        ce->slots[s].count = 0;
        for (t = 0; t < ce->ntasks; ++t) {
            rx_runtime *rt = ce->tasks[t].rt;
            for (i = 0; i < rt->node_count; ++i) {
                long p = rt->nodes[i].period_us;
                if (p > 0 && ((long)s * base_us) % p == 0) {
                    if (ce->slots[s].count >=
                        (int)(RXNET_CE_MAX_TASKS * RXNET_MAX_RUNTIME_NODES)) {
                        return -1;
                    }
                    ce->slots[s].activation[ce->slots[s].count].rt = rt;
                    ce->slots[s].activation[ce->slots[s].count].node_idx =
                        (unsigned char)i;
                    ce->slots[s].count++;
                }
            }
        }

        sort_slot_by_deadline(&ce->slots[s]);

        for (t = 0; t < ce->ntasks; ++t) {
            rx_runtime *rt = ce->tasks[t].rt;
            for (i = 0; i < rt->node_count; ++i) {
                if (rt->nodes[i].period_us == 0) {
                    if (ce->slots[s].count >=
                        (int)(RXNET_CE_MAX_TASKS * RXNET_MAX_RUNTIME_NODES)) {
                        return -1;
                    }
                    ce->slots[s].activation[ce->slots[s].count].rt = rt;
                    ce->slots[s].activation[ce->slots[s].count].node_idx =
                        (unsigned char)i;
                    ce->slots[s].count++;
                }
            }
        }
    }

    ce->base_us = base_us;
    ce->nslots  = nslots;
    return 0;
}

int
rx_cyclic_exec_enable_sched_check(rx_cyclic_exec *ce, int enabled)
{
    if (ce == NULL) return -1;
    ce->sched_check_enabled = enabled ? 1 : 0;
    return 0;
}

static void
sched_report_reset(rx_sched_report *report)
{
    if (report == NULL) return;
    memset(report, 0, sizeof(*report));
    report->schedulable = 1;
}

static int
sched_report_add(rx_sched_report *report, const rx_ce_activation *a,
                 long interference_us, long blocking_us,
                 long response_us, int schedulable)
{
    rx_sched_task_result *r;
    rx_node_entry *entry;

    if (report == NULL) return 0;
    if (report->task_count >= (int)RXNET_SCHED_MAX_TASKS) return 0;

    r = &report->tasks[report->task_count++];
    entry = &a->rt->nodes[a->node_idx];
    r->rt = a->rt;
    r->node_idx = a->node_idx;
    r->period_us = entry->period_us;
    r->deadline_us = effective_deadline(entry);
    r->wcet_us = entry->wcet_us;
    r->interference_us = interference_us;
    r->blocking_us = blocking_us;
    r->response_us = response_us;
    r->schedulable = schedulable;
    r->resource_access_count = entry->resource_access_count;
    {
        int i;
        for (i = 0; i < entry->resource_access_count; ++i) {
            r->resource_accesses[i] = entry->resource_accesses[i];
        }
    }
    if (!schedulable) report->schedulable = 0;
    return 0;
}

int
rx_cyclic_exec_check_schedulability(rx_cyclic_exec *ce,
                                    rx_sched_report *report,
                                    FILE *log)
{
    int s, a;
    int all_schedulable = 1;

    if (ce == NULL) return RX_SCHED_ERROR;
    if (ce->nslots == 0 && rx_cyclic_exec_build(ce) != 0) {
        if (log) fprintf(log, "cyclic: build failed\n");
        return RX_SCHED_ERROR;
    }

    sched_report_reset(report);
    if (log) {
        fprintf(log, "cyclic schedulability: base=%ld us slots=%d\n",
                ce->base_us, ce->nslots);
    }

    for (s = 0; s < ce->nslots; ++s) {
        long elapsed_us = 0;
        long slot_start_us = (long)s * ce->base_us;
        for (a = 0; a < ce->slots[s].count; ++a) {
            rx_ce_activation *act = &ce->slots[s].activation[a];
            rx_node_entry *entry = &act->rt->nodes[act->node_idx];
            long d = effective_deadline(entry);
            long c = entry->wcet_us;
            long finish_us;
            int ok;

            if (entry->period_us <= 0) {
                if (log) fprintf(log, "cyclic: async node cannot be analysed\n");
                return RX_SCHED_UNSUPPORTED;
            }
            if (c <= 0) {
                if (log) fprintf(log, "cyclic: missing WCET for node %u\n",
                                 (unsigned)act->node_idx);
                return RX_SCHED_ERROR;
            }

            finish_us = elapsed_us + c;
            ok = finish_us <= d;
            if (!ok) all_schedulable = 0;
            if (sched_report_add(report, act, elapsed_us, 0, finish_us, ok) != 0) {
                return RX_SCHED_ERROR;
            }
            if (log) {
                fprintf(log,
                        "slot=%d t=%ld node=%u C=%ld D=%ld start=%ld finish=%ld %s\n",
                        s, slot_start_us, (unsigned)act->node_idx, c, d,
                        elapsed_us, finish_us, ok ? "OK" : "MISS");
            }
            elapsed_us = finish_us;
        }
    }

    return all_schedulable ? RX_SCHED_SCHEDULABLE : RX_SCHED_UNSCHEDULABLE;
}

void
rx_cyclic_exec_run(rx_cyclic_exec *ce)
{
    rx_tick_t next_tick;
    long activation_us;
    int slot, s, t;

    if (ce->nslots == 0 && rx_cyclic_exec_build(ce) != 0)
        return;

    if (ce->sched_check_enabled &&
        rx_cyclic_exec_check_schedulability(ce, NULL, NULL) == RX_SCHED_UNSCHEDULABLE) {
        return;
    }

    next_tick = rx_tick_now();
    activation_us = 0;
    slot = 0;
    ce->stop_requested = 0;

    while (!ce->stop_requested) {
        for (t = 0; t < ce->ntasks; ++t) {
            unsigned char active[RXNET_MAX_RUNTIME_NODES];
            int count = 0;
            rx_runtime *rt = ce->tasks[t].rt;

            for (s = 0; s < ce->slots[slot].count; ++s) {
                rx_ce_activation *a = &ce->slots[slot].activation[s];
                if (a->rt == rt) {
                    active[count++] = a->node_idx;
                }
            }
            if (count > 0) {
                rx_runtime_tick_nodes_at(rt, active, count, activation_us);
            }
            if (ce->stop_requested) break;
        }

        next_tick = rx_tick_add_us(next_tick, ce->base_us);
        activation_us += ce->base_us;
        if (!ce->stop_requested)
            rx_tick_sleep_until(next_tick);

        slot = (slot + 1) % ce->nslots;
    }

    if (ce->on_stop)
        ce->on_stop(ce->on_stop_user);
}

void
rx_cyclic_exec_stop(rx_cyclic_exec *ce)
{
    ce->stop_requested = 1;
}

void
rx_cyclic_exec_on_stop(rx_cyclic_exec *ce,
                       void (*callback)(void *user),
                       void *user)
{
    ce->on_stop = callback;
    ce->on_stop_user = user;
}
