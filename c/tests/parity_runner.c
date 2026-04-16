/*
 * parity_runner.c — C-side conformance runner.
 *
 * Runs a fixed set of shared scenarios and prints one line per tick:
 *
 *   <scenario_name> <tick> <key>=<value>
 *
 * The Python counterpart (python/tests/parity_runner.py) must produce
 * identical output for every scenario.  run_parity.sh diffs the two.
 *
 * Scenarios
 * ---------
 * fsm_light  — FSM toggle (OFF/ON), 6 ticks: press no press no press no
 * pn_light   — PN toggle  (P_OFF/P_ON), same 6-tick input sequence
 * fsm_first_match — two transitions from same state, first-match wins
 */

#include <stdio.h>
#include <stddef.h>

#include "rxnet/fsm.h"
#include "rxnet/pn.h"

/* ------------------------------------------------------------------ */
/* Shared input injection                                               */
/* ------------------------------------------------------------------ */

static int g_button = 0;   /* set before each tick by the scenario runner */

/* ------------------------------------------------------------------ */
/* FSM helpers                                                          */
/* ------------------------------------------------------------------ */

/* Latch: copy g_button into per-machine int held via user pointer. */
static void fsm_latch(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    *(int *)user = g_button;
}

static void fsm_noop_dump(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    (void)user;
}

static int fsm_guard_user(const rx_fsm_context *ctx, void *user) {
    (void)ctx;
    return *(int *)user;
}

/* ------------------------------------------------------------------ */
/* Scenario: fsm_light                                                  */
/* ------------------------------------------------------------------ */

static void run_fsm_light(void) {
    static const rx_fsm_transition transitions[] = {
        {0, 1, fsm_guard_user, NULL},
        {1, 0, fsm_guard_user, NULL},
    };
    static const int sequence[] = {1, 0, 1, 0, 1, 0};
    static const int N = 6;

    int button = 0;
    rx_fsm_machine machine;
    rx_fsm_runtime rt;
    int i;

    rx_fsm_runtime_init(&rt, 1);
    rx_fsm_machine_init(&machine, "light", 0, transitions, 2,
                        &button, fsm_latch, fsm_noop_dump);
    rx_fsm_runtime_add_machine(&rt, &machine);

    for (i = 0; i < N; i++) {
        g_button = sequence[i];
        rx_fsm_tick(&rt);
        printf("fsm_light %d state=%d\n", i + 1, machine.state);
    }
}

/* ------------------------------------------------------------------ */
/* Scenario: fsm_first_match                                            */
/* Always fires A→B (not A→C) because both guards pass and first wins. */
/* ------------------------------------------------------------------ */

static void run_fsm_first_match(void) {
    static const rx_fsm_transition transitions[] = {
        {0, 1, NULL, NULL},   /* A→B unconditional */
        {0, 2, NULL, NULL},   /* A→C unconditional, must NOT fire */
    };
    static const int N = 3;

    int button = 0;
    rx_fsm_machine machine;
    rx_fsm_runtime rt;
    int i;

    rx_fsm_runtime_init(&rt, 1);
    /* Machine starts at 0; first tick → B(1), then B has no outgoing → stays 1 */
    rx_fsm_machine_init(&machine, "first_match", 0, transitions, 2,
                        &button, fsm_latch, fsm_noop_dump);
    rx_fsm_runtime_add_machine(&rt, &machine);

    for (i = 0; i < N; i++) {
        rx_fsm_tick(&rt);
        printf("fsm_first_match %d state=%d\n", i + 1, machine.state);
    }
}

/* ------------------------------------------------------------------ */
/* PN helpers                                                           */
/* ------------------------------------------------------------------ */

enum {
    PN_LIGHT_P_OFF     = 0,
    PN_LIGHT_P_ON      = 1,
    PN_LIGHT_P_REQUEST = 2,
};

static void pn_light_latch_cb(rx_pn_context *ctx, void *user) {
    rx_pn_net *net = (rx_pn_net *)user;
    (void)ctx;
    if (g_button) {
        net->places[PN_LIGHT_P_REQUEST]++;
    }
}

/* ------------------------------------------------------------------ */
/* Scenario: pn_light                                                   */
/* ------------------------------------------------------------------ */

static void run_pn_light(void) {
    static const rx_pn_arc on_consume[]  = {{PN_LIGHT_P_OFF, 1}, {PN_LIGHT_P_REQUEST, 1}};
    static const rx_pn_arc on_produce[]  = {{PN_LIGHT_P_ON,  1}};
    static const rx_pn_arc off_consume[] = {{PN_LIGHT_P_ON,  1}, {PN_LIGHT_P_REQUEST, 1}};
    static const rx_pn_arc off_produce[] = {{PN_LIGHT_P_OFF, 1}};
    static const rx_pn_transition transitions[] = {
        {on_consume,  2, on_produce,  1, NULL, NULL},
        {off_consume, 2, off_produce, 1, NULL, NULL},
    };
    static const int initial[] = {1, 0, 0};
    static const int sequence[] = {1, 0, 1, 0, 1, 0};
    static const int N = 6;

    rx_pn_net net;
    rx_pn_runtime rt;
    int i;

    rx_pn_runtime_init(&rt, 1);
    rx_pn_net_init(&net, "light", initial, 3, transitions, 2, &net,
                   pn_light_latch_cb, NULL);
    rx_pn_runtime_add_net(&rt, &net);

    for (i = 0; i < N; i++) {
        g_button = sequence[i];
        rx_pn_tick(&rt);
        printf("pn_light %d p_on=%d\n", i + 1, net.places[PN_LIGHT_P_ON]);
    }

    rx_pn_net_free(&net);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    run_fsm_light();
    run_fsm_first_match();
    run_pn_light();
    return 0;
}
