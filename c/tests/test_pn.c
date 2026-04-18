#include "rxtest.h"
#include "rxnet/pn.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int g_action_calls = 0;
static void counting_action(rx_pn_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_action_calls++;
}

static int g_guard_return = 1;
static int guard_from_flag(const rx_pn_context *ctx, void *user) {
    (void)ctx; (void)user;
    return g_guard_return;
}

/* Place index constants */
enum { P0 = 0, P1 = 1, P2 = 2 };

/* ------------------------------------------------------------------ */
/* Net builder helpers                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    rx_pn_runtime rt;
    rx_pn_net     net;
} pn_fixture;

static int pn_fixture_init(
    pn_fixture *f,
    const int *initial_places,
    size_t place_count,
    const rx_pn_transition *transitions,
    size_t transition_count
) {
    if (rx_pn_runtime_init(&f->rt, 1) != 0) return -1;
    if (rx_pn_net_init(&f->net, "test", initial_places, place_count,
                       transitions, transition_count, NULL, NULL, NULL) != 0) return -1;
    return rx_pn_runtime_add_net(&f->rt, &f->net, 0, 0);
}

static void pn_fixture_free(pn_fixture *f) {
    rx_pn_net_free(&f->net);
}

/* ------------------------------------------------------------------ */
/* Tests: net lifecycle                                                 */
/* ------------------------------------------------------------------ */

static void pn_net_init_rejects_null_net(void) {
    ASSERT_EQ(-1, rx_pn_net_init(NULL, "x", NULL, 0, NULL, 0, NULL, NULL, NULL));
}

static void pn_net_init_rejects_null_places_when_count_nonzero(void) {
    rx_pn_net net;
    ASSERT_EQ(-1, rx_pn_net_init(&net, "x", NULL, 2, NULL, 0, NULL, NULL, NULL));
}

static void pn_net_init_rejects_null_transitions_when_count_nonzero(void) {
    rx_pn_net net;
    int places[] = {1};
    ASSERT_EQ(-1, rx_pn_net_init(&net, "x", places, 1, NULL, 1, NULL, NULL, NULL));
}

static void pn_net_init_rejects_out_of_range_place_id(void) {
    rx_pn_net net;
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = 99, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
    }};
    ASSERT_EQ(-1, rx_pn_net_init(&net, "x", places, 1, trans, 1, NULL, NULL, NULL));
}

static void pn_net_init_rejects_negative_arc_weight(void) {
    rx_pn_net net;
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = 0, .weight = -1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
    }};
    ASSERT_EQ(-1, rx_pn_net_init(&net, "x", places, 1, trans, 1, NULL, NULL, NULL));
}

static void pn_net_free_null_is_safe(void) {
    rx_pn_net_free(NULL);
}

static void pn_net_free_sets_pointers_to_null(void) {
    rx_pn_net net;
    int places[] = {1, 2};
    rx_pn_net_init(&net, "x", places, 2, NULL, 0, NULL, NULL, NULL);
    rx_pn_net_free(&net);
    ASSERT_NULL(net.places);
    ASSERT_NULL(net.next_places);
    ASSERT_NULL(net.fire_flags);
}

static void pn_net_destroy_null_is_safe(void) {
    rx_pn_net_destroy(NULL);
}

static void pn_runtime_destroy_null_is_safe(void) {
    rx_pn_runtime_destroy(NULL);
}

/* ------------------------------------------------------------------ */
/* Tests: place initialization                                          */
/* ------------------------------------------------------------------ */

static void pn_places_initialized_from_initial_array(void) {
    int places[] = {3, 0, 7};
    rx_pn_net net;
    rx_pn_net_init(&net, "x", places, 3, NULL, 0, NULL, NULL, NULL);
    ASSERT_EQ(3, net.places[0]);
    ASSERT_EQ(0, net.places[1]);
    ASSERT_EQ(7, net.places[2]);
    rx_pn_net_free(&net);
}

/* ------------------------------------------------------------------ */
/* Tests: transition firing                                             */
/* ------------------------------------------------------------------ */

static void pn_transition_fires_when_tokens_available(void) {
    int places[] = {1}; /* P0 = 1 */
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
    }};
    pn_fixture f;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]); /* token consumed */
    pn_fixture_free(&f);
}

static void pn_transition_does_not_fire_when_tokens_insufficient(void) {
    int places[] = {0}; /* P0 = 0, not enough */
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
    }};
    pn_fixture f;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]); /* unchanged */
    pn_fixture_free(&f);
}

static void pn_transition_consumes_and_produces_tokens(void) {
    /* P0=2, P1=0 — transition consumes 2 from P0, produces 1 in P1 */
    int places[] = {2, 0};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 2 }};
    static const rx_pn_arc produce[] = {{ .place_id = P1, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = produce, .produce_count = 1,
    }};
    pn_fixture f;
    pn_fixture_init(&f, places, 2, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]);
    ASSERT_EQ(1, f.net.places[P1]);
    pn_fixture_free(&f);
}

static void pn_transition_partial_tokens_does_not_fire(void) {
    /* Needs 3, only has 2 */
    int places[] = {2};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 3 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
    }};
    pn_fixture f;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(2, f.net.places[P0]); /* unchanged */
    pn_fixture_free(&f);
}

static void pn_transition_with_no_consume_always_fires(void) {
    /* No consume arc → always enabled; produces 1 token in P0 */
    int places[] = {0};
    static const rx_pn_arc produce[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = NULL,    .consume_count = 0,
        .produce = produce, .produce_count = 1,
    }};
    pn_fixture f;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(1, f.net.places[P0]);
    pn_fixture_free(&f);
}

/* ------------------------------------------------------------------ */
/* Tests: guards                                                        */
/* ------------------------------------------------------------------ */

static void pn_guard_prevents_firing_when_false(void) {
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
        .guard   = guard_from_flag,
    }};
    pn_fixture f;
    g_guard_return = 0;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(1, f.net.places[P0]); /* unchanged — guard blocked it */
    pn_fixture_free(&f);
}

static void pn_guard_allows_firing_when_true(void) {
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
        .guard   = guard_from_flag,
    }};
    pn_fixture f;
    g_guard_return = 1;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]); /* consumed */
    pn_fixture_free(&f);
}

/* ------------------------------------------------------------------ */
/* Tests: deferred actions                                              */
/* ------------------------------------------------------------------ */

static void pn_action_fires_after_tick(void) {
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
        .action  = counting_action,
    }};
    pn_fixture f;
    g_action_calls = 0;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(1, g_action_calls);
    pn_fixture_free(&f);
}

static void pn_action_not_called_when_transition_does_not_fire(void) {
    int places[] = {0}; /* not enough tokens */
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
        .action  = counting_action,
    }};
    pn_fixture f;
    g_action_calls = 0;
    pn_fixture_init(&f, places, 1, trans, 1);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, g_action_calls);
    pn_fixture_free(&f);
}

/* ------------------------------------------------------------------ */
/* Tests: tick reset behavior                                           */
/* ------------------------------------------------------------------ */

static void pn_fire_flags_reset_each_tick(void) {
    /* P0=1: first tick fires (consumes token). Second tick: no tokens, no fire. */
    int places[] = {1};
    static const rx_pn_arc consume[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {{
        .consume = consume, .consume_count = 1,
        .produce = NULL,    .produce_count = 0,
        .action  = counting_action,
    }};
    pn_fixture f;
    g_action_calls = 0;
    pn_fixture_init(&f, places, 1, trans, 1);

    rx_pn_tick(&f.rt); /* fires: P0 → 0 */
    rx_pn_tick(&f.rt); /* should not fire: P0 == 0 */

    ASSERT_EQ(1, g_action_calls); /* action called exactly once */
    pn_fixture_free(&f);
}

static void pn_conflicting_transitions_first_match_wins(void) {
    /*
     * Greedy sequential semantics (Option A):
     * Transitions are evaluated in declaration order against next_places,
     * which is updated immediately when each transition fires.
     *
     * P0 = 1 token. T0 and T1 both consume 1 token from P0.
     * T0 evaluates first: next_places[P0] = 1 >= 1 → fires, next_places[P0] = 0.
     * T1 evaluates next:  next_places[P0] = 0 <  1 → does not fire.
     * Result: P0 = 0. Places never go negative.
     */
    int places[] = {1};
    static const rx_pn_arc consume_t0[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_arc consume_t1[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {
        { .consume = consume_t0, .consume_count = 1, .produce = NULL, .produce_count = 0 },
        { .consume = consume_t1, .consume_count = 1, .produce = NULL, .produce_count = 0 },
    };
    pn_fixture f;
    pn_fixture_init(&f, places, 1, trans, 2);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]); /* only T0 fired */
    pn_fixture_free(&f);
}

static void pn_conflicting_transitions_second_wins_when_first_blocked_by_guard(void) {
    /*
     * T0 has tokens but its guard returns false → skipped.
     * T1 then sees next_places[P0] still = 1 → fires.
     */
    int places[] = {1};
    static const rx_pn_arc consume_t0[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_arc consume_t1[] = {{ .place_id = P0, .weight = 1 }};
    static const rx_pn_transition trans[] = {
        { .consume = consume_t0, .consume_count = 1, .produce = NULL, .produce_count = 0,
          .guard = guard_from_flag },
        { .consume = consume_t1, .consume_count = 1, .produce = NULL, .produce_count = 0 },
    };
    pn_fixture f;
    g_guard_return = 0; /* T0 guard blocks */
    pn_fixture_init(&f, places, 1, trans, 2);
    rx_pn_tick(&f.rt);
    ASSERT_EQ(0, f.net.places[P0]); /* T1 fired */
    pn_fixture_free(&f);
}

/* ------------------------------------------------------------------ */
/* Tests: PN runtime                                                    */
/* ------------------------------------------------------------------ */

static void pn_runtime_init_rejects_null(void) {
    ASSERT_EQ(-1, rx_pn_runtime_init(NULL, 1));
}

static void pn_tick_null_returns_error(void) {
    ASSERT_EQ(-1, rx_pn_tick(NULL));
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    TEST_SUITE("pn net lifecycle");
    RUN_TEST(pn_net_init_rejects_null_net);
    RUN_TEST(pn_net_init_rejects_null_places_when_count_nonzero);
    RUN_TEST(pn_net_init_rejects_null_transitions_when_count_nonzero);
    RUN_TEST(pn_net_init_rejects_out_of_range_place_id);
    RUN_TEST(pn_net_init_rejects_negative_arc_weight);
    RUN_TEST(pn_net_free_null_is_safe);
    RUN_TEST(pn_net_free_sets_pointers_to_null);
    RUN_TEST(pn_net_destroy_null_is_safe);
    RUN_TEST(pn_runtime_destroy_null_is_safe);

    TEST_SUITE("pn place initialization");
    RUN_TEST(pn_places_initialized_from_initial_array);

    TEST_SUITE("pn transition firing");
    RUN_TEST(pn_transition_fires_when_tokens_available);
    RUN_TEST(pn_transition_does_not_fire_when_tokens_insufficient);
    RUN_TEST(pn_transition_consumes_and_produces_tokens);
    RUN_TEST(pn_transition_partial_tokens_does_not_fire);
    RUN_TEST(pn_transition_with_no_consume_always_fires);

    TEST_SUITE("pn guards");
    RUN_TEST(pn_guard_prevents_firing_when_false);
    RUN_TEST(pn_guard_allows_firing_when_true);

    TEST_SUITE("pn actions");
    RUN_TEST(pn_action_fires_after_tick);
    RUN_TEST(pn_action_not_called_when_transition_does_not_fire);

    TEST_SUITE("pn tick semantics");
    RUN_TEST(pn_fire_flags_reset_each_tick);
    RUN_TEST(pn_conflicting_transitions_first_match_wins);
    RUN_TEST(pn_conflicting_transitions_second_wins_when_first_blocked_by_guard);

    TEST_SUITE("pn runtime");
    RUN_TEST(pn_runtime_init_rejects_null);
    RUN_TEST(pn_tick_null_returns_error);

    TEST_SUMMARY();
}
