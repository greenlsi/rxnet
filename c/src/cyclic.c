#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
/* Time utilities                                                       */
/* ------------------------------------------------------------------ */

struct timespec
rx_timespec_add_us(struct timespec t, long us)
{
    t.tv_nsec += us * 1000L;
    while (t.tv_nsec >= 1000000000L) {
        t.tv_nsec -= 1000000000L;
        ++t.tv_sec;
    }
    return t;
}

int
rx_timespec_compare(struct timespec a, struct timespec b)
{
    if (a.tv_sec  != b.tv_sec)  return a.tv_sec  < b.tv_sec  ? -1 : 1;
    if (a.tv_nsec != b.tv_nsec) return a.tv_nsec < b.tv_nsec ? -1 : 1;
    return 0;
}

void
rx_sleep_until(struct timespec target)
{
    struct timespec now, rem;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (rx_timespec_compare(now, target) >= 0) return;
        rem.tv_sec  = target.tv_sec  - now.tv_sec;
        rem.tv_nsec = target.tv_nsec - now.tv_nsec;
        if (rem.tv_nsec < 0) { rem.tv_nsec += 1000000000L; --rem.tv_sec; }
        if (rem.tv_sec < 0) return;
        if (nanosleep(&rem, NULL) == 0) return;
        if (errno != EINTR) return;
    }
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
    struct timespec next_tick;
    int slot, s;

    if (ce->nslots == 0)
        rx_cyclic_exec_build(ce);

    clock_gettime(CLOCK_MONOTONIC, &next_tick);
    slot = 0;

    for (;;) {
        for (s = 0; s < ce->slots[slot].count; ++s)
            ce->slots[slot].rt[s]->tick(ce->slots[slot].rt[s]);

        next_tick = rx_timespec_add_us(next_tick, ce->base_us);
        rx_sleep_until(next_tick);

        slot = (slot + 1) % ce->nslots;
    }
}
