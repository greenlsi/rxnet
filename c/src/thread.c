// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "rxnet/thread.h"
#include "rxnet/trace.h"

/* ------------------------------------------------------------------ */
/* Node loop                                                            */
/* ------------------------------------------------------------------ */

static void
touch_wcet(rx_runtime *rt, int idx, rx_tick_t start, rx_tick_t end)
{
    long elapsed_us = (long)((end - start + 999) / 1000);
    if (elapsed_us < 1) elapsed_us = 1;
    if (elapsed_us > rt->nodes[idx].wcet_us)
        rt->nodes[idx].wcet_us = elapsed_us;
}

static long
effective_deadline(const rx_node_entry *entry)
{
    return entry->deadline_us > 0 ? entry->deadline_us : entry->period_us;
}

static int
thread_task_less(rx_runtime *a_rt, unsigned char a_idx,
                 rx_runtime *b_rt, unsigned char b_idx)
{
    long da = effective_deadline(&a_rt->nodes[a_idx]);
    long db = effective_deadline(&b_rt->nodes[b_idx]);
    if (da != db) return da < db;
    if (a_rt != b_rt) return (uintptr_t)a_rt < (uintptr_t)b_rt;
    return a_idx < b_idx;
}

static int
thread_total_nodes(const rx_thread_exec *te)
{
    int total = 0;
    int g;

    for (g = 0; g < te->ngroups; ++g)
        total += (int)te->groups[g].rt->node_count;
    return total;
}

static int
thread_priority_rank(const rx_thread_exec *te, rx_runtime *rt, unsigned char idx)
{
    int rank = 0;
    int g;

    for (g = 0; g < te->ngroups; ++g) {
        rx_runtime *other_rt = te->groups[g].rt;
        size_t n;
        for (n = 0; n < other_rt->node_count; ++n) {
            if (other_rt == rt && n == idx) continue;
            if (thread_task_less(other_rt, (unsigned char)n, rt, idx))
                rank++;
        }
    }
    return rank;
}

typedef struct {
    rx_runtime *rt;
    unsigned char node_idx;
} thread_sched_task_ref;

static int
resource_used(const int *used_resources, int used_count, int resource_id)
{
    int i;
    for (i = 0; i < used_count; ++i) {
        if (used_resources[i] == resource_id) return 1;
    }
    return 0;
}

static long
max_thread_blocking_rec(const thread_sched_task_ref *tasks,
                        int pos,
                        int count,
                        int *used_resources,
                        int used_count)
{
    long best;
    rx_node_entry *entry;
    int i;

    if (pos >= count) return 0;
    best = max_thread_blocking_rec(tasks, pos + 1, count,
                                   used_resources, used_count);
    entry = &tasks[pos].rt->nodes[tasks[pos].node_idx];
    for (i = 0; i < entry->resource_access_count; ++i) {
        int resource_id = entry->resource_accesses[i].resource_id;
        long candidate;
        if (resource_used(used_resources, used_count, resource_id)) continue;
        used_resources[used_count] = resource_id;
        candidate = entry->resource_accesses[i].max_us +
                    max_thread_blocking_rec(tasks, pos + 1, count,
                                            used_resources, used_count + 1);
        if (candidate > best) best = candidate;
    }
    return best;
}

static long
max_thread_blocking(const thread_sched_task_ref *tasks, int first_lower, int count)
{
    int used_resources[RXNET_MAX_SHARED_RESOURCES];
    return max_thread_blocking_rec(&tasks[first_lower], 0, count - first_lower,
                                   used_resources, 0);
}

static int
active_count_at(const rx_thread_group *grp, long activation_us)
{
    int count = 0;
    size_t n;

    for (n = 0; n < grp->rt->node_count; ++n) {
        long p = grp->rt->nodes[n].period_us;
        if (p > 0 && activation_us % p == 0) count++;
    }
    return count;
}

static rx_thread_activation_group *
activation_group_get(rx_thread_group *grp, long activation_us,
                     rx_tick_t activation_tick)
{
    rx_thread_activation_group *free_group = NULL;
    rx_thread_activation_group *found = NULL;
    int i;

    rx_mutex_lock(&grp->activation_lock);
    for (i = 0; i < (int)RXNET_THREAD_MAX_ACTIVE_GROUPS; ++i) {
        if (grp->active[i].in_use && grp->active[i].activation_us == activation_us) {
            found = &grp->active[i];
            break;
        }
        if (!grp->active[i].in_use && free_group == NULL)
            free_group = &grp->active[i];
    }

    if (found == NULL && free_group != NULL) {
        int count = active_count_at(grp, activation_us);
        if (count > 0) {
            found = free_group;
            found->in_use = 1;
            found->activation_us = activation_us;
            found->activation_tick = activation_tick;
            found->count = count;
            found->done = 0;
            rx_barrier_reset(&found->eval_b, (unsigned int)count);
            rx_barrier_reset(&found->commit_b, (unsigned int)count);
        }
    }
    rx_mutex_unlock(&grp->activation_lock);
    return found;
}

static void
activation_group_done(rx_thread_group *grp, rx_thread_activation_group *ag)
{
    rx_mutex_lock(&grp->activation_lock);
    ag->done++;
    if (ag->done == ag->count) {
        ag->in_use = 0;
    }
    rx_mutex_unlock(&grp->activation_lock);
}

static void
node_loop(rx_thread_arg *a)
{
    rx_thread_exec  *te    = a->te;
    rx_thread_group *grp   = &te->groups[a->group_idx];
    rx_runtime      *rt    = grp->rt;
    int              idx   = a->node_idx;
    rx_node         *node  = rt->nodes[idx].node;
    rx_context      *ctx   = &grp->node_ctx[idx];
    long      period_us = rt->nodes[idx].period_us;
    long      activation_us = 0;

    for (;;) {
        rx_thread_activation_group *ag;
        rx_tick_t activation_tick;
        rx_tick_t start, end;

        activation_tick = rx_tick_add_us(te->t0, activation_us);
        rx_tick_sleep_until(activation_tick);
        ag = activation_group_get(grp, activation_us, activation_tick);
        if (ag == NULL) {
            fprintf(stderr, "rxnet: thread_exec_run: active group pool exhausted\n");
            te->stop_requested = 1;
            return;
        }

        rx_context_set_activation_us(ctx, ag->activation_us);
        ctx->active_entry = &rt->nodes[idx];
        ctx->shared_resources = &rt->shared_resources;
        ctx->critical_locking_enabled = 1;
        RX_TRACE_NODE_START(node);
        start = rx_tick_now();
        RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
        node->vtable->latch_inputs(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
        RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
        node->vtable->evaluate(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);

        rx_barrier_wait(&ag->eval_b);
        RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
        node->vtable->commit(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
        rx_context_dispatch_deferred(ctx);
        rx_barrier_wait(&ag->commit_b);
        RX_TRACE_PH_START(node, RX_TRACE_PH_DUMP);
        node->vtable->dump_outputs(node, ctx);
        ctx->active_entry = NULL;
        RX_TRACE_PH_END(node, RX_TRACE_PH_DUMP);
        end = rx_tick_now();
        touch_wcet(rt, idx, start, end);
        RX_TRACE_NODE_END(node);
        activation_group_done(grp, ag);

        activation_us += period_us;
        if (te->stop_requested)
            return;
    }
}

static void *
thread_entry(void *arg)
{
    node_loop((rx_thread_arg *)arg);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
rx_thread_exec_init(rx_thread_exec *te)
{
    memset(te, 0, sizeof(*te));
}

int
rx_thread_exec_enable_sched_check(rx_thread_exec *te, int enabled)
{
    if (te == NULL) return -1;
    te->sched_check_enabled = enabled ? 1 : 0;
    return 0;
}

int
rx_thread_exec_check_schedulability(rx_thread_exec *te,
                                    rx_sched_report *report,
                                    FILE *log)
{
    int ret;
    int i, j;

    if (te == NULL) return RX_SCHED_ERROR;
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }
#ifndef RXNET_THREAD_FIFO_AVAILABLE
    (void)i;
    (void)j;
    if (report != NULL) report->unsupported = 1;
    if (log) {
        fprintf(log, "thread schedulability: FIFO priorities unavailable\n");
    }
    return RX_SCHED_UNSUPPORTED;
#else
    if (te->fifo_config_attempted && !te->fifo_configured) {
        if (report != NULL) report->unsupported = 1;
        if (log) {
            fprintf(log, "thread schedulability: FIFO priorities were not configured\n");
        }
        return RX_SCHED_UNSUPPORTED;
    }
    {
        thread_sched_task_ref tasks[RXNET_SCHED_MAX_TASKS];
        int count = 0;
        int all_schedulable = 1;

        if (report != NULL) report->schedulable = 1;
        for (i = 0; i < te->ngroups; ++i) {
            rx_runtime *rt = te->groups[i].rt;
            size_t n;
            for (n = 0; n < rt->node_count; ++n) {
                if (rt->nodes[n].period_us <= 0) {
                    if (report != NULL) report->unsupported = 1;
                    if (log) fprintf(log, "thread: async node cannot be analysed\n");
                    return RX_SCHED_UNSUPPORTED;
                }
                if (rt->nodes[n].wcet_us <= 0) {
                    if (log) fprintf(log, "thread: missing WCET for node %u\n",
                                     (unsigned)n);
                    return RX_SCHED_ERROR;
                }
                if (count >= (int)RXNET_SCHED_MAX_TASKS) return RX_SCHED_ERROR;
                tasks[count].rt = rt;
                tasks[count].node_idx = (unsigned char)n;
                count++;
            }
        }

        for (i = 1; i < count; ++i) {
            thread_sched_task_ref tmp = tasks[i];
            j = i;
            while (j > 0) {
                int less = thread_task_less(tmp.rt, tmp.node_idx,
                                            tasks[j - 1].rt,
                                            tasks[j - 1].node_idx);
                if (!less) break;
                tasks[j] = tasks[j - 1];
                --j;
            }
            tasks[j] = tmp;
        }

        if (log) fprintf(log, "thread schedulability: tasks=%d\n", count);

        for (i = 0; i < count; ++i) {
            rx_node_entry *ei = &tasks[i].rt->nodes[tasks[i].node_idx];
            long ci = ei->wcet_us;
            long di = effective_deadline(ei);
            long bi = max_thread_blocking(tasks, i + 1, count);
            long initial_interference = 0;
            long ri_prev;
            long ri;
            int converged = 0;

            for (j = 0; j < i; ++j) {
                initial_interference += tasks[j].rt->nodes[tasks[j].node_idx].wcet_us;
            }
            ri_prev = ci + bi + initial_interference;
            ri = ri_prev;

            for (;;) {
                long interference = 0;
                for (j = 0; j < i; ++j) {
                    rx_node_entry *ej = &tasks[j].rt->nodes[tasks[j].node_idx];
                    interference += ((ri_prev + ej->period_us - 1) / ej->period_us) *
                                    ej->wcet_us;
                }
                ri = ci + bi + interference;
                if (ri == ri_prev) {
                    converged = 1;
                    break;
                }
                if (ri > di) break;
                ri_prev = ri;
            }

            if (report != NULL && report->task_count < (int)RXNET_SCHED_MAX_TASKS) {
                rx_sched_task_result *tr = &report->tasks[report->task_count++];
                tr->rt = tasks[i].rt;
                tr->node_idx = tasks[i].node_idx;
                tr->period_us = ei->period_us;
                tr->deadline_us = di;
                tr->wcet_us = ci;
                tr->blocking_us = bi;
                tr->response_us = ri;
                tr->interference_us = ri - ci - bi;
                tr->schedulable = converged && ri <= di;
                tr->resource_access_count = ei->resource_access_count;
                {
                    int r;
                    for (r = 0; r < ei->resource_access_count; ++r) {
                        tr->resource_accesses[r] = ei->resource_accesses[r];
                    }
                }
            }
            if (!(converged && ri <= di)) {
                all_schedulable = 0;
                if (report != NULL) report->schedulable = 0;
            }
            if (log) {
                fprintf(log, "node=%u C=%ld T=%ld D=%ld B=%ld I=%ld R=%ld %s\n",
                        (unsigned)tasks[i].node_idx, ci, ei->period_us, di,
                        bi, ri - ci - bi, ri,
                        (converged && ri <= di) ? "OK" : "MISS");
            }
        }
        ret = all_schedulable ? RX_SCHED_SCHEDULABLE : RX_SCHED_UNSCHEDULABLE;
    }
    return ret;
#endif
}

int
rx_thread_exec_add(rx_thread_exec *te, rx_runtime *rt)
{
    rx_thread_group *grp;
    size_t i;
    int    s;

    if (te->ngroups >= (int)RXNET_THREAD_MAX_RUNTIMES) {
        fprintf(stderr, "rxnet: thread_exec_add: max runtimes (%u) exceeded\n",
                RXNET_THREAD_MAX_RUNTIMES);
        return -1;
    }

    if (rt->nslots == 0) {
        if (rx_runtime_build(rt) != 0) return -1;
    }

    if (rt->period_us == 0) {
        fprintf(stderr, "rxnet: thread_exec_add: runtime has no periodic nodes "
                "(all async); cannot determine tick rate\n");
        return -1;
    }

    grp = &te->groups[te->ngroups];
    grp->rt = rt;
    grp->thread_count = (int)rt->node_count;
    rx_mutex_init(&grp->activation_lock);

    for (i = 0; i < rt->node_count; i++) {
        if (rt->nodes[i].period_us <= 0) {
            fprintf(stderr, "rxnet: thread_exec_add: async nodes are not supported\n");
            return -1;
        }
        if (rx_context_init(&grp->node_ctx[i]) != 0) return -1;
        grp->node_ctx[i].shared_resources = &rt->shared_resources;
        grp->node_ctx[i].critical_locking_enabled = 1;
    }

    for (s = 0; s < (int)RXNET_THREAD_MAX_ACTIVE_GROUPS; s++) {
        grp->active[s].in_use = 0;
        if (rx_barrier_init(&grp->active[s].eval_b,
                            (unsigned int)RXNET_MAX_RUNTIME_NODES) != 0) return -1;
        if (rx_barrier_init(&grp->active[s].commit_b,
                            (unsigned int)RXNET_MAX_RUNTIME_NODES) != 0) return -1;
    }

    te->ngroups++;
    return 0;
}

void
rx_thread_exec_run(rx_thread_exec *te)
{
    int g, i;
    int last_g = te->ngroups - 1;
    int last_n;
    int total_nodes;

    if (te->ngroups == 0) return;
    total_nodes = thread_total_nodes(te);

    if (te->sched_check_enabled &&
        rx_thread_exec_check_schedulability(te, NULL, NULL) == RX_SCHED_UNSCHEDULABLE) {
        return;
    }
    te->t0 = rx_tick_now();
    te->stop_requested = 0;
    te->fifo_configured = 1;
    te->fifo_config_attempted = 1;

    /* Spawn all nodes except the last node of the last group. */
    for (g = 0; g < te->ngroups; g++) {
        rx_thread_group *grp = &te->groups[g];
        int n = (int)grp->rt->node_count;
        int limit = (g == last_g) ? n - 1 : n;

        for (i = 0; i < limit; i++) {
            grp->args[i].te        = te;
            grp->args[i].group_idx = g;
            grp->args[i].node_idx  = i;
            grp->args[i].priority_rank =
                thread_priority_rank(te, grp->rt, (unsigned char)i);
            if (rx_thread_create(&grp->tids[i],
                                thread_entry, &grp->args[i]) != 0) {
                fprintf(stderr,
                        "rxnet: thread_exec_run: rx_thread_create failed "
                        "(group %d, node %d)\n", g, i);
                grp->tids[i] = 0;
                te->fifo_configured = 0;
            } else if (rx_thread_configure_fifo(&grp->tids[i],
                       grp->args[i].priority_rank,
                       (int)total_nodes) != 0) {
                te->fifo_configured = 0;
            }
        }
    }

    /* Last node of last group runs on the calling (main) thread. */
    {
        rx_thread_group *grp = &te->groups[last_g];
        last_n = (int)grp->rt->node_count - 1;
        grp->args[last_n].te        = te;
        grp->args[last_n].group_idx = last_g;
        grp->args[last_n].node_idx  = last_n;
        grp->args[last_n].priority_rank =
            thread_priority_rank(te, grp->rt, (unsigned char)last_n);
        if (rx_thread_configure_current_fifo(grp->args[last_n].priority_rank,
                                             (int)total_nodes) != 0) {
            te->fifo_configured = 0;
        }
        node_loop(&grp->args[last_n]);
    }

    te->stop_requested = 1;
    if (te->on_stop)
        te->on_stop(te->on_stop_user);
}

void
rx_thread_exec_stop(rx_thread_exec *te)
{
    te->stop_requested = 1;
}

void
rx_thread_exec_on_stop(rx_thread_exec *te,
                       void (*callback)(void *user),
                       void *user)
{
    te->on_stop = callback;
    te->on_stop_user = user;
}
