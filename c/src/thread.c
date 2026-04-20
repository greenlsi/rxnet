// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "rxnet/thread.h"
#include "rxnet/trace.h"

/* ------------------------------------------------------------------ */
/* Node loop                                                            */
/* ------------------------------------------------------------------ */

static void
node_loop(rx_thread_arg *a)
{
    rx_thread_exec  *te    = a->te;
    rx_thread_group *grp   = &te->groups[a->group_idx];
    rx_runtime      *rt    = grp->rt;
    int              idx   = a->node_idx;
    rx_node         *node  = rt->nodes[idx].node;
    rx_context      *ctx   = &grp->node_ctx[idx];
    long      period_us = rt->nodes[idx].period_us > 0
                          ? rt->nodes[idx].period_us : rt->period_us;
    int       step      = (rt->period_us > 0)
                          ? (int)(period_us / rt->period_us) : 1;
    rx_tick_t next      = te->t0;
    int       base_tick = 0;

    for (;;) {
        int slot;

        rx_tick_sleep_until(next);
        slot = base_tick % rt->nslots;

        rx_barrier_wait(&grp->latch_b[slot]);
        RX_TRACE_NODE_START(node);
        RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
        node->vtable->latch_inputs(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
        RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
        node->vtable->evaluate(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);

        rx_barrier_wait(&grp->commit_b[slot]);
        RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
        node->vtable->commit(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
        rx_context_dispatch_deferred(ctx);
        RX_TRACE_PH_START(node, RX_TRACE_PH_DUMP);
        node->vtable->dump_outputs(node, ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_DUMP);
        RX_TRACE_NODE_END(node);

        next = rx_tick_add_us(next, period_us);
        base_tick += step;
    }
}

static void
thread_entry(void *arg)
{
    node_loop((rx_thread_arg *)arg);
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

    for (i = 0; i < rt->node_count; i++) {
        if (rx_context_init(&grp->node_ctx[i]) != 0) return -1;
    }

    for (s = 0; s < rt->nslots; s++) {
        unsigned int count = (unsigned int)rt->slots_storage[s].count;
        if (rx_barrier_init(&grp->latch_b[s],  count) != 0) return -1;
        if (rx_barrier_init(&grp->commit_b[s], count) != 0) return -1;
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

    te->t0 = rx_tick_now();

    /* Spawn all nodes except the last node of the last group. */
    for (g = 0; g < te->ngroups; g++) {
        rx_thread_group *grp = &te->groups[g];
        int n = (int)grp->rt->node_count;
        int limit = (g == last_g) ? n - 1 : n;

        for (i = 0; i < limit; i++) {
            grp->args[i].te        = te;
            grp->args[i].group_idx = g;
            grp->args[i].node_idx  = i;
            if (rx_thread_create(&grp->tids[i],
                                thread_entry, &grp->args[i]) != 0) {
                fprintf(stderr,
                        "rxnet: thread_exec_run: rx_thread_create failed "
                        "(group %d, node %d)\n", g, i);
                grp->tids[i] = 0;
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
        node_loop(&grp->args[last_n]); /* never returns */
    }
}
