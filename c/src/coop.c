// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include <stdio.h>
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

    if (rt->nslots == 0) {
        if (rx_runtime_build(rt) != 0) return -1;
    }

    if (rt->period_us == 0) {
        fprintf(stderr, "rxnet: coop_exec_add: runtime has no periodic nodes "
                "(all async); cannot determine call rate\n");
        return -1;
    }

    ce->tasks[ce->ntasks].rt = rt;
    ++ce->ntasks;
    return 0;
}

void
rx_coop_exec_run(rx_coop_exec *ce)
{
    rx_tick_t now, nearest;
    int i;

    /* Initialise all deadlines to "now" so every task fires on the first pass. */
    now = rx_tick_now();
    for (i = 0; i < ce->ntasks; ++i)
        ce->tasks[i].next_tick = now;
    ce->stop_requested = 0;

    while (!ce->stop_requested) {
        now = rx_tick_now();

        /* Run every runtime whose deadline has passed. */
        for (i = 0; i < ce->ntasks; ++i) {
            if (rx_tick_compare(now, ce->tasks[i].next_tick) >= 0) {
                ce->tasks[i].rt->tick(ce->tasks[i].rt);
                /* Advance deadline by one period (tracks drift rather than
                 * resetting from now, preventing accumulated phase slippage). */
                ce->tasks[i].next_tick =
                    rx_tick_add_us(ce->tasks[i].next_tick,
                                   ce->tasks[i].rt->period_us);
                if (ce->stop_requested) break;
            }
        }

        /* Sleep until the nearest upcoming deadline. */
        nearest = ce->tasks[0].next_tick;
        for (i = 1; i < ce->ntasks; ++i) {
            if (rx_tick_compare(ce->tasks[i].next_tick, nearest) < 0)
                nearest = ce->tasks[i].next_tick;
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
