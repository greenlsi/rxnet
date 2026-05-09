// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "rxtest.h"
#include "rxnet/fsm.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* No-op latch / dump used when we don't need them */
static void noop_latch(rx_fsm_context *ctx, void *user) { (void)ctx; (void)user; }
static void noop_dump(rx_fsm_context *ctx, void *user)  { (void)ctx; (void)user; }

/* Guard that always returns true */
static int guard_true(const rx_fsm_context *ctx, void *user)  { (void)ctx; (void)user; return 1; }

/* Guard that always returns false */
static int guard_false(const rx_fsm_context *ctx, void *user) { (void)ctx; (void)user; return 0; }

/* Guard that reads a flag from user data */
static int guard_flag(const rx_fsm_context *ctx, void *user) {
    (void)ctx;
    return *(int *)user;
}

/* Counting action */
static int g_action_calls  = 0;
static void *g_action_user = NULL;

static void counting_action(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    g_action_calls++;
    g_action_user = user;
}

/* State constants */
enum { STATE_A = 0, STATE_B = 1, STATE_C = 2 };

/* ------------------------------------------------------------------ */
/* Machine builder helpers                                              */
/* ------------------------------------------------------------------ */

/*
 * Build a minimal runtime + machine wired together.
 * Caller provides the transition table and initial state.
 */
typedef struct {
    rx_fsm_runtime rt;
    rx_fsm_machine machine;
} fsm_fixture;

static int fsm_fixture_init(
    fsm_fixture *f,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user
) {
    if (rx_fsm_runtime_init(&f->rt, 1) != 0) return -1;
    rx_fsm_machine_init(&f->machine, "test", initial_state,
                        transitions, transition_count, user,
                        noop_latch, noop_dump);
    return rx_fsm_runtime_add_machine(&f->rt, &f->machine, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Tests: lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void fsm_runtime_init_rejects_null(void) {
    ASSERT_EQ(-1, rx_fsm_runtime_init(NULL, 1));
}

static void fsm_runtime_init_succeeds(void) {
    rx_fsm_runtime rt;
    ASSERT_EQ(0, rx_fsm_runtime_init(&rt, 1));
}

static void fsm_runtime_create_returns_non_null(void) {
    rx_fsm_runtime *rt = rx_fsm_runtime_create(1);
    ASSERT_NOT_NULL(rt);
    rx_fsm_runtime_destroy(rt);
}

static void fsm_runtime_destroy_null_is_safe(void) {
    rx_fsm_runtime_destroy(NULL);
}

static void fsm_add_machine_accepts_null_latch_as_noop(void) {
    rx_fsm_runtime rt;
    rx_fsm_machine m;

    ASSERT_EQ(0, rx_fsm_runtime_init(&rt, 1));
    rx_fsm_machine_init(&m, "m", 0, NULL, 0, NULL, NULL, noop_dump);
    ASSERT_EQ(0, rx_fsm_runtime_add_machine(&rt, &m, 0, 0));
    ASSERT_EQ(0, rx_fsm_tick(&rt));

    rx_fsm_runtime_free(&rt);
}

static void fsm_add_machine_accepts_null_dump_as_noop(void) {
    rx_fsm_runtime rt;
    rx_fsm_machine m;

    ASSERT_EQ(0, rx_fsm_runtime_init(&rt, 1));
    rx_fsm_machine_init(&m, "m", 0, NULL, 0, NULL, noop_latch, NULL);
    ASSERT_EQ(0, rx_fsm_runtime_add_machine(&rt, &m, 0, 0));
    ASSERT_EQ(0, rx_fsm_tick(&rt));

    rx_fsm_runtime_free(&rt);
}

static void fsm_machine_destroy_null_is_safe(void) {
    rx_fsm_machine_destroy(NULL);
}

/* ------------------------------------------------------------------ */
/* Tests: state and transitions                                         */
/* ------------------------------------------------------------------ */

static void fsm_machine_starts_in_initial_state(void) {
    rx_fsm_machine m;
    rx_fsm_machine_init(&m, "m", STATE_B, NULL, 0, NULL, noop_latch, noop_dump);
    ASSERT_EQ(STATE_B, m.state);
}

static void fsm_no_guard_always_transitions(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_B, f.machine.state);
}

static void fsm_true_guard_transitions(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = guard_true, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_B, f.machine.state);
}

static void fsm_false_guard_stays_in_state(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = guard_false, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_A, f.machine.state);
}

static void fsm_no_matching_from_state_stays_in_state(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_B, .to_state = STATE_C, .guard = NULL, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_A, f.machine.state);
}

static void fsm_first_match_in_declaration_order_wins(void) {
    /*
     * Two transitions from STATE_A: first goes to B, second (never reached) to C.
     * Both have no guard, so first match must win.
     */
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL, .action = NULL },
        { .from_state = STATE_A, .to_state = STATE_C, .guard = NULL, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 2, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_B, f.machine.state);
}

static void fsm_skips_false_guard_takes_second_match(void) {
    /*
     * First transition has false guard, second has no guard.
     * Must skip first, take second.
     */
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = guard_false, .action = NULL },
        { .from_state = STATE_A, .to_state = STATE_C, .guard = NULL,        .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 2, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_C, f.machine.state);
}

static void fsm_guard_reads_user_data(void) {
    int flag = 1;
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = guard_flag, .action = NULL },
    };
    fsm_fixture f;
    fsm_fixture_init(&f, STATE_A, transitions, 1, &flag);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_B, f.machine.state);

    flag = 0;
    f.machine.state = STATE_A;
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(STATE_A, f.machine.state);
}

/* ------------------------------------------------------------------ */
/* Tests: actions                                                       */
/* ------------------------------------------------------------------ */

static void fsm_action_is_called_after_tick(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL, .action = counting_action },
    };
    fsm_fixture f;
    g_action_calls = 0;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(1, g_action_calls);
}

static void fsm_action_not_called_when_no_transition(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = guard_false, .action = counting_action },
    };
    fsm_fixture f;
    g_action_calls = 0;
    fsm_fixture_init(&f, STATE_A, transitions, 1, NULL);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ(0, g_action_calls);
}

static void fsm_action_receives_machine_user_data(void) {
    int sentinel = 99;
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL, .action = counting_action },
    };
    fsm_fixture f;
    g_action_user = NULL;
    fsm_fixture_init(&f, STATE_A, transitions, 1, &sentinel);
    rx_fsm_tick(&f.rt);
    ASSERT_EQ((long long)&sentinel, (long long)g_action_user);
}

/*
 * The action must be deferred: it runs AFTER the commit phase updates the
 * machine state. So when the action fires, state is already STATE_B.
 *
 * We verify this by capturing state inside the action callback.
 */
static int g_state_at_action_time = -1;

static void state_capturing_action(rx_fsm_context *ctx, void *user) {
    (void)ctx;
    g_state_at_action_time = ((fsm_fixture *)user)->machine.state;
}

static void fsm_action_fires_after_state_is_committed(void) {
    static const rx_fsm_transition transitions[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL,
          .action = state_capturing_action },
    };
    fsm_fixture f;
    g_state_at_action_time = -1;
    fsm_fixture_init(&f, STATE_A, transitions, 1, &f);
    rx_fsm_tick(&f.rt);
    /* State must be B (committed) when the action runs */
    ASSERT_EQ(STATE_B, g_state_at_action_time);
}

/* ------------------------------------------------------------------ */
/* Tests: multiple machines                                             */
/* ------------------------------------------------------------------ */

static void fsm_two_machines_tick_independently(void) {
    static const rx_fsm_transition trans_m1[] = {
        { .from_state = STATE_A, .to_state = STATE_B, .guard = NULL, .action = NULL },
    };
    static const rx_fsm_transition trans_m2[] = {
        { .from_state = STATE_A, .to_state = STATE_C, .guard = NULL, .action = NULL },
    };

    rx_fsm_runtime rt;
    rx_fsm_machine m1, m2;

    rx_fsm_runtime_init(&rt, 2);

    rx_fsm_machine_init(&m1, "m1", STATE_A, trans_m1, 1, NULL, noop_latch, noop_dump);
    rx_fsm_machine_init(&m2, "m2", STATE_A, trans_m2, 1, NULL, noop_latch, noop_dump);

    rx_fsm_runtime_add_machine(&rt, &m1, 0, 0);
    rx_fsm_runtime_add_machine(&rt, &m2, 0, 0);

    rx_fsm_tick(&rt);

    ASSERT_EQ(STATE_B, m1.state);
    ASSERT_EQ(STATE_C, m2.state);
}

static void fsm_tick_null_returns_error(void) {
    ASSERT_EQ(-1, rx_fsm_tick(NULL));
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    TEST_SUITE("fsm lifecycle");
    RUN_TEST(fsm_runtime_init_rejects_null);
    RUN_TEST(fsm_runtime_init_succeeds);
    RUN_TEST(fsm_runtime_create_returns_non_null);
    RUN_TEST(fsm_runtime_destroy_null_is_safe);
    RUN_TEST(fsm_add_machine_accepts_null_latch_as_noop);
    RUN_TEST(fsm_add_machine_accepts_null_dump_as_noop);
    RUN_TEST(fsm_machine_destroy_null_is_safe);

    TEST_SUITE("fsm state transitions");
    RUN_TEST(fsm_machine_starts_in_initial_state);
    RUN_TEST(fsm_no_guard_always_transitions);
    RUN_TEST(fsm_true_guard_transitions);
    RUN_TEST(fsm_false_guard_stays_in_state);
    RUN_TEST(fsm_no_matching_from_state_stays_in_state);
    RUN_TEST(fsm_first_match_in_declaration_order_wins);
    RUN_TEST(fsm_skips_false_guard_takes_second_match);
    RUN_TEST(fsm_guard_reads_user_data);

    TEST_SUITE("fsm actions");
    RUN_TEST(fsm_action_is_called_after_tick);
    RUN_TEST(fsm_action_not_called_when_no_transition);
    RUN_TEST(fsm_action_receives_machine_user_data);
    RUN_TEST(fsm_action_fires_after_state_is_committed);

    TEST_SUITE("fsm multi-machine");
    RUN_TEST(fsm_two_machines_tick_independently);
    RUN_TEST(fsm_tick_null_returns_error);

    TEST_SUMMARY();
}
