// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

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

    /* Build slot table if not already done. */
    if (rt->nslots == 0) {
        if (rx_runtime_build(rt) != 0) return -1;
    }

    if (rt->period_us == 0) {
        fprintf(stderr, "rxnet: cyclic_exec_add: runtime has no periodic nodes "
                "(all async); cannot determine call rate\n");
        return -1;
    }

    ce->tasks[ce->ntasks].rt        = rt;
    ce->tasks[ce->ntasks].period_us = rt->period_us;
    ++ce->ntasks;
    return 0;
}

int
rx_cyclic_exec_build(rx_cyclic_exec *ce)
{
    long base_us, hyper_us;
    int  nslots, s, t;

    if (ce->ntasks == 0) return -1;

    base_us  = ce->tasks[0].period_us;
    hyper_us = ce->tasks[0].period_us;
    for (t = 1; t < ce->ntasks; ++t) {
        base_us  = gcd(base_us,  ce->tasks[t].period_us);
        hyper_us = lcm(hyper_us, ce->tasks[t].period_us);
    }

    nslots = (int)(hyper_us / base_us);
    if (nslots > (int)RXNET_CE_MAX_SLOTS) return -1;

    for (s = 0; s < nslots; ++s) {
        ce->slots[s].count = 0;
        for (t = 0; t < ce->ntasks; ++t) {
            if ((s * base_us) % ce->tasks[t].period_us == 0)
                ce->slots[s].rt[ce->slots[s].count++] = ce->tasks[t].rt;
        }
    }

    ce->base_us = base_us;
    ce->nslots  = nslots;
    return 0;
}

void
rx_cyclic_exec_run(rx_cyclic_exec *ce)
{
    rx_tick_t next_tick;
    int slot, s;

    if (ce->nslots == 0)
        rx_cyclic_exec_build(ce);

    next_tick = rx_tick_now();
    slot = 0;

    for (;;) {
        for (s = 0; s < ce->slots[slot].count; ++s)
            ce->slots[slot].rt[s]->tick(ce->slots[slot].rt[s]);

        next_tick = rx_tick_add_us(next_tick, ce->base_us);
        rx_tick_sleep_until(next_tick);

        slot = (slot + 1) % ce->nslots;
    }
}
