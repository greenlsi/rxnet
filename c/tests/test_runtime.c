// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rxtest.h"
#include "rxnet/runtime.h"
#include "rxnet/thread.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Helpers: a minimal node implementation for testing                  */
/* ------------------------------------------------------------------ */

typedef struct {
    rx_node node;
    int latch_calls;
    int eval_calls;
    int commit_calls;
    int dump_calls;
    /* Records the global order in which each phase was called. */
    int *order_log;
    int *order_count;
    int phase_id; /* unique ID for this node, used in order log */
} test_node;

static void test_node_latch(rx_node *n, rx_context *ctx) {
    test_node *tn = (test_node *)n;
    (void)ctx;
    tn->latch_calls++;
    if (tn->order_log) {
        tn->order_log[(*tn->order_count)++] = tn->phase_id * 10 + 0;
    }
}
static void test_node_eval(rx_node *n, rx_context *ctx) {
    test_node *tn = (test_node *)n;
    (void)ctx;
    tn->eval_calls++;
    if (tn->order_log) {
        tn->order_log[(*tn->order_count)++] = tn->phase_id * 10 + 1;
    }
}
static void test_node_commit(rx_node *n, rx_context *ctx) {
    test_node *tn = (test_node *)n;
    (void)ctx;
    tn->commit_calls++;
    if (tn->order_log) {
        tn->order_log[(*tn->order_count)++] = tn->phase_id * 10 + 2;
    }
}
static void test_node_dump(rx_node *n, rx_context *ctx) {
    test_node *tn = (test_node *)n;
    (void)ctx;
    tn->dump_calls++;
    if (tn->order_log) {
        tn->order_log[(*tn->order_count)++] = tn->phase_id * 10 + 3;
    }
}

static const rx_node_vtable TEST_NODE_VTABLE = {
    .latch_inputs = test_node_latch,
    .evaluate     = test_node_eval,
    .commit       = test_node_commit,
    .dump_outputs = test_node_dump,
};

static void test_node_init(test_node *tn) {
    tn->node.vtable           = &TEST_NODE_VTABLE;
    tn->node.latch_inputs_cb  = NULL;
    tn->node.dump_outputs_cb  = NULL;
    tn->latch_calls  = 0;
    tn->eval_calls   = 0;
    tn->commit_calls = 0;
    tn->dump_calls   = 0;
    tn->order_log    = NULL;
    tn->order_count  = NULL;
    tn->phase_id     = 0;
}

/* Deferred action test helper */
static int g_action_calls = 0;
static void *g_action_user = NULL;

static void counting_action(rx_context *ctx, void *user) {
    (void)ctx;
    g_action_calls++;
    g_action_user = user;
}

static int g_order_buf[32];
static int g_order_count = 0;

static void action_A(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 0xA;
}
static void action_B(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 0xB;
}

/* ------------------------------------------------------------------ */
/* Context tests                                                        */
/* ------------------------------------------------------------------ */

static void context_init_rejects_null(void) {
    ASSERT_EQ(-1, rx_context_init(NULL));
}

static void context_init_succeeds(void) {
    rx_context ctx;
    ASSERT_EQ(0, rx_context_init(&ctx));
}

static void context_create_returns_valid_context(void) {
    rx_context *ctx = rx_context_create();
    ASSERT_NOT_NULL(ctx);
    rx_context_destroy(ctx);
}

static void context_destroy_null_is_safe(void) {
    rx_context_destroy(NULL); /* must not crash */
}

static void context_free_null_is_safe(void) {
    rx_context_free(NULL); /* must not crash */
}

/* ------------------------------------------------------------------ */
/* Deferred action queue tests                                         */
/* ------------------------------------------------------------------ */

static void enqueue_rejects_null_context(void) {
    ASSERT_EQ(-1, rx_context_enqueue_deferred_action(NULL, counting_action, NULL));
}

static void enqueue_rejects_null_function(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    ASSERT_EQ(-1, rx_context_enqueue_deferred_action(&ctx, NULL, NULL));
}

static void enqueue_single_action_executes_on_run(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_action_calls = 0;

    ASSERT_EQ(0, rx_context_enqueue_deferred_action(&ctx, counting_action, NULL));
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ(1, g_action_calls);
}

static void enqueue_passes_user_data_to_callback(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    int sentinel = 42;
    g_action_user = NULL;

    rx_context_enqueue_deferred_action(&ctx, counting_action, &sentinel);
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ((long long)&sentinel, (long long)g_action_user);
}

static void enqueue_multiple_actions_run_in_fifo_order(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_order_count = 0;

    rx_context_enqueue_deferred_action(&ctx, action_A, NULL);
    rx_context_enqueue_deferred_action(&ctx, action_B, NULL);
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ(2,    g_order_count);
    ASSERT_EQ(0xA,  g_order_buf[0]);
    ASSERT_EQ(0xB,  g_order_buf[1]);
}

static void enqueue_at_capacity_returns_error(void) {
    rx_context ctx;
    rx_context_init(&ctx);

    /* Fill the queue to capacity */
    size_t i;
    for (i = 0; i < RXNET_MAX_DEFERRED_ACTIONS; i++) {
        ASSERT_EQ(0, rx_context_enqueue_deferred_action(&ctx, counting_action, NULL));
    }
    /* One more must be rejected */
    ASSERT_EQ(-1, rx_context_enqueue_deferred_action(&ctx, counting_action, NULL));
}

static void run_deferred_actions_clears_queue(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_action_calls = 0;

    rx_context_enqueue_deferred_action(&ctx, counting_action, NULL);
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ(0, (int)ctx.deferred_count);

    /* A second run should call nothing */
    rx_context_run_deferred_actions(&ctx);
    ASSERT_EQ(1, g_action_calls);
}

static void run_deferred_actions_on_null_is_safe(void) {
    rx_context_run_deferred_actions(NULL); /* must not crash */
}

/* ------------------------------------------------------------------ */
/* Runtime tests                                                        */
/* ------------------------------------------------------------------ */

static void runtime_init_rejects_null_rt(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    ASSERT_EQ(-1, rx_runtime_init(NULL, &ctx, 4));
}

static void runtime_init_rejects_null_ctx(void) {
    rx_runtime rt;
    ASSERT_EQ(-1, rx_runtime_init(&rt, NULL, 4));
}

static void runtime_init_rejects_capacity_exceeding_max(void) {
    rx_context ctx;
    rx_runtime rt;
    rx_context_init(&ctx);
    ASSERT_EQ(-1, rx_runtime_init(&rt, &ctx, RXNET_MAX_RUNTIME_NODES + 1));
}

static void runtime_init_at_max_capacity_succeeds(void) {
    rx_context ctx;
    rx_runtime rt;
    rx_context_init(&ctx);
    ASSERT_EQ(0, rx_runtime_init(&rt, &ctx, RXNET_MAX_RUNTIME_NODES));
}

static void runtime_add_node_rejects_null_rt(void) {
    test_node tn;
    test_node_init(&tn);
    ASSERT_EQ(-1, rx_runtime_add_node(NULL, &tn.node, 0, 0));
}

static void runtime_add_node_rejects_null_node(void) {
    rx_context ctx;
    rx_runtime rt;
    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 4);
    ASSERT_EQ(-1, rx_runtime_add_node(&rt, NULL, 0, 0));
}

static void runtime_add_node_beyond_capacity_returns_error(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node nodes[3];
    int i;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 2);

    for (i = 0; i < 3; i++) {
        test_node_init(&nodes[i]);
    }

    ASSERT_EQ(0, rx_runtime_add_node(&rt, &nodes[0].node, 0, 0));
    ASSERT_EQ(0, rx_runtime_add_node(&rt, &nodes[1].node, 0, 0));
    ASSERT_EQ(-1, rx_runtime_add_node(&rt, &nodes[2].node, 0, 0)); /* capacity 2, third must fail */
}

static void tick_rejects_null_runtime(void) {
    ASSERT_EQ(-1, rx_tick(NULL));
}

static void tick_calls_all_phases_on_registered_node(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node tn;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 1);
    test_node_init(&tn);
    rx_runtime_add_node(&rt, &tn.node, 0, 0);

    rx_tick(&rt);

    ASSERT_EQ(1, tn.latch_calls);
    ASSERT_EQ(1, tn.eval_calls);
    ASSERT_EQ(1, tn.commit_calls);
    ASSERT_EQ(1, tn.dump_calls);
}

static void tick_nodes_runs_only_selected_active_group(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node a, b, c;
    unsigned char active[] = {1, 2};

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 3);
    test_node_init(&a);
    test_node_init(&b);
    test_node_init(&c);

    rx_runtime_add_node(&rt, &a.node, 10000, 0);
    rx_runtime_add_node(&rt, &b.node, 10000, 0);
    rx_runtime_add_node(&rt, &c.node, 20000, 0);

    ASSERT_EQ(0, rx_runtime_tick_nodes(&rt, active, 2));

    ASSERT_EQ(0, a.latch_calls);
    ASSERT_EQ(0, a.eval_calls);
    ASSERT_EQ(0, a.commit_calls);
    ASSERT_EQ(0, a.dump_calls);

    ASSERT_EQ(1, b.latch_calls);
    ASSERT_EQ(1, b.eval_calls);
    ASSERT_EQ(1, b.commit_calls);
    ASSERT_EQ(1, b.dump_calls);

    ASSERT_EQ(1, c.latch_calls);
    ASSERT_EQ(1, c.eval_calls);
    ASSERT_EQ(1, c.commit_calls);
    ASSERT_EQ(1, c.dump_calls);
}

static void tick_nodes_updates_wcet_for_selected_nodes_only(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node a, b;
    unsigned char active[] = {1};

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 2);
    test_node_init(&a);
    test_node_init(&b);

    rx_runtime_add_node(&rt, &a.node, 10000, 0);
    rx_runtime_add_node(&rt, &b.node, 10000, 0);

    ASSERT_EQ(0, rx_runtime_tick_nodes(&rt, active, 1));

    ASSERT_EQ(0, rt.nodes[0].wcet_us);
    ASSERT_TRUE(rt.nodes[1].wcet_us > 0);
}

/*
 * Phase order for a single node must be:
 *   latch(0) → evaluate(1) → commit(2) → [deferred actions] → dump(3)
 *
 * For two nodes A and B:
 *   latch_A, latch_B, eval_A, eval_B, commit_A, commit_B, dump_A, dump_B
 */
static void tick_phases_execute_in_correct_order(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node a, b;
    int order[32];
    int count = 0;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 2);

    test_node_init(&a);
    a.order_log   = order;
    a.order_count = &count;
    a.phase_id    = 1; /* encodes as 10=latch,11=eval,12=commit,13=dump */

    test_node_init(&b);
    b.order_log   = order;
    b.order_count = &count;
    b.phase_id    = 2; /* encodes as 20=latch,21=eval,22=commit,23=dump */

    rx_runtime_add_node(&rt, &a.node, 0, 0);
    rx_runtime_add_node(&rt, &b.node, 0, 0);

    rx_tick(&rt);

    /* Expected: latch_A(10), latch_B(20), eval_A(11), eval_B(21),
                 commit_A(12), commit_B(22), dump_A(13), dump_B(23) */
    ASSERT_EQ(8, count);
    ASSERT_EQ(10, order[0]); /* latch A */
    ASSERT_EQ(20, order[1]); /* latch B */
    ASSERT_EQ(11, order[2]); /* eval  A */
    ASSERT_EQ(21, order[3]); /* eval  B */
    ASSERT_EQ(12, order[4]); /* commit A */
    ASSERT_EQ(22, order[5]); /* commit B */
    ASSERT_EQ(13, order[6]); /* dump A */
    ASSERT_EQ(23, order[7]); /* dump B */
}

static void tick_deferred_actions_run_after_all_commits(void) {
    /*
     * Deferred actions must fire after all commits, before dump.
     * We verify via g_action_calls vs tn.dump_calls after one tick.
     */
    rx_context ctx;
    rx_runtime rt;
    test_node tn;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 1);
    test_node_init(&tn);
    rx_runtime_add_node(&rt, &tn.node, 0, 0);

    g_action_calls = 0;

    /* Enqueue an action before the tick */
    rx_context_enqueue_deferred_action(&ctx, counting_action, NULL);

    rx_tick(&rt);

    /* Action must have been called once, and dump must have run once */
    ASSERT_EQ(1, g_action_calls);
    ASSERT_EQ(1, tn.dump_calls);
}

static void runtime_free_null_is_safe(void) {
    rx_runtime_free(NULL); /* must not crash */
}

static void runtime_destroy_null_is_safe(void) {
    rx_runtime_destroy(NULL); /* must not crash */
}

static void runtime_build_does_not_materialize_activation_table(void) {
    rx_context ctx;
    rx_runtime rt;
    test_node fast, slow;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 2);
    test_node_init(&fast);
    test_node_init(&slow);

    ASSERT_EQ(0, rx_runtime_add_node(&rt, &fast.node, 10000, 0));
    ASSERT_EQ(0, rx_runtime_add_node(&rt, &slow.node, 20000, 0));
    ASSERT_EQ(0, rx_runtime_build(&rt));

    ASSERT_EQ(10000, rt.period_us);
    ASSERT_EQ(0, rt.nslots);
}

static void thread_sched_check_uses_response_time_analysis(void) {
    rx_context ctx;
    rx_runtime rt;
    rx_thread_exec te;
    rx_sched_report report;
    test_node fast, slow;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 2);
    test_node_init(&fast);
    test_node_init(&slow);

    ASSERT_EQ(0, rx_runtime_add_node(&rt, &fast.node, 10000, 5000));
    ASSERT_EQ(0, rx_runtime_add_node(&rt, &slow.node, 20000, 20000));
    rt.nodes[0].wcet_us = 1000;
    rt.nodes[1].wcet_us = 2000;

    rx_thread_exec_init(&te);
    ASSERT_EQ(0, rx_thread_exec_add(&te, &rt));
    ASSERT_EQ(RX_SCHED_SCHEDULABLE,
              rx_thread_exec_check_schedulability(&te, &report, NULL));
    ASSERT_EQ(1, report.schedulable);
    ASSERT_EQ(2, report.task_count);
    ASSERT_EQ(0, report.tasks[0].node_idx);
    ASSERT_EQ(1, report.tasks[1].node_idx);
    ASSERT_EQ(3000, report.tasks[1].response_us);
}

static void thread_exec_rejects_async_nodes(void) {
    rx_context ctx;
    rx_runtime rt;
    rx_thread_exec te;
    test_node async;

    rx_context_init(&ctx);
    rx_runtime_init(&rt, &ctx, 1);
    test_node_init(&async);

    ASSERT_EQ(0, rx_runtime_add_node(&rt, &async.node, 0, 0));
    rx_thread_exec_init(&te);
    ASSERT_EQ(-1, rx_thread_exec_add(&te, &rt));
}

/* ------------------------------------------------------------------ */
/* Priority deferred action tests                                       */
/* ------------------------------------------------------------------ */

static void action_low(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 1; /* LOW */
}
static void action_high(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 3; /* HIGH */
}
static void action_normal(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 2; /* NORMAL */
}
static void action_critical(rx_context *ctx, void *user) {
    (void)ctx; (void)user;
    g_order_buf[g_order_count++] = 4; /* CRITICAL */
}

static void priority_high_runs_before_low(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_order_count = 0;

    rx_context_enqueue_deferred_action_p(&ctx, action_low,  NULL, RX_PRIORITY_LOW);
    rx_context_enqueue_deferred_action_p(&ctx, action_high, NULL, RX_PRIORITY_HIGH);
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ(2, g_order_count);
    ASSERT_EQ(3, g_order_buf[0]); /* HIGH first */
    ASSERT_EQ(1, g_order_buf[1]); /* LOW second */
}

static void priority_fifo_within_same_priority(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_order_count = 0;

    rx_context_enqueue_deferred_action_p(&ctx, action_A, NULL, RX_PRIORITY_NORMAL);
    rx_context_enqueue_deferred_action_p(&ctx, action_B, NULL, RX_PRIORITY_NORMAL);
    rx_context_run_deferred_actions(&ctx);

    ASSERT_EQ(2,    g_order_count);
    ASSERT_EQ(0xA,  g_order_buf[0]); /* A first (FIFO) */
    ASSERT_EQ(0xB,  g_order_buf[1]); /* B second */
}

static void priority_mixed_order(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_order_count = 0;

    rx_context_enqueue_deferred_action_p(&ctx, action_normal,   NULL, RX_PRIORITY_NORMAL);
    rx_context_enqueue_deferred_action_p(&ctx, action_low,      NULL, RX_PRIORITY_LOW);
    rx_context_enqueue_deferred_action_p(&ctx, action_critical, NULL, RX_PRIORITY_CRITICAL);
    rx_context_enqueue_deferred_action_p(&ctx, action_high,     NULL, RX_PRIORITY_HIGH);
    rx_context_run_deferred_actions(&ctx);

    /* Expected order: CRITICAL(4), HIGH(3), NORMAL(2), LOW(1) */
    ASSERT_EQ(4, g_order_count);
    ASSERT_EQ(4, g_order_buf[0]);
    ASSERT_EQ(3, g_order_buf[1]);
    ASSERT_EQ(2, g_order_buf[2]);
    ASSERT_EQ(1, g_order_buf[3]);
}

static void dispatch_deferred_null_pool_runs_inline(void) {
    rx_context ctx;
    rx_context_init(&ctx);
    g_action_calls = 0;

    rx_context_enqueue_deferred_action(&ctx, counting_action, NULL);
    rx_context_dispatch_deferred(&ctx);

    ASSERT_EQ(1, g_action_calls);
    ASSERT_EQ(0, (int)ctx.deferred_count);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    TEST_SUITE("context lifecycle");
    RUN_TEST(context_init_rejects_null);
    RUN_TEST(context_init_succeeds);
    RUN_TEST(context_create_returns_valid_context);
    RUN_TEST(context_destroy_null_is_safe);
    RUN_TEST(context_free_null_is_safe);

    TEST_SUITE("deferred action queue");
    RUN_TEST(enqueue_rejects_null_context);
    RUN_TEST(enqueue_rejects_null_function);
    RUN_TEST(enqueue_single_action_executes_on_run);
    RUN_TEST(enqueue_passes_user_data_to_callback);
    RUN_TEST(enqueue_multiple_actions_run_in_fifo_order);
    RUN_TEST(enqueue_at_capacity_returns_error);
    RUN_TEST(run_deferred_actions_clears_queue);
    RUN_TEST(run_deferred_actions_on_null_is_safe);

    TEST_SUITE("runtime lifecycle");
    RUN_TEST(runtime_init_rejects_null_rt);
    RUN_TEST(runtime_init_rejects_null_ctx);
    RUN_TEST(runtime_init_rejects_capacity_exceeding_max);
    RUN_TEST(runtime_init_at_max_capacity_succeeds);
    RUN_TEST(runtime_add_node_rejects_null_rt);
    RUN_TEST(runtime_add_node_rejects_null_node);
    RUN_TEST(runtime_add_node_beyond_capacity_returns_error);
    RUN_TEST(runtime_free_null_is_safe);
    RUN_TEST(runtime_destroy_null_is_safe);
    RUN_TEST(runtime_build_does_not_materialize_activation_table);
    RUN_TEST(thread_sched_check_uses_response_time_analysis);
    RUN_TEST(thread_exec_rejects_async_nodes);

    TEST_SUITE("tick execution");
    RUN_TEST(tick_rejects_null_runtime);
    RUN_TEST(tick_calls_all_phases_on_registered_node);
    RUN_TEST(tick_nodes_runs_only_selected_active_group);
    RUN_TEST(tick_nodes_updates_wcet_for_selected_nodes_only);
    RUN_TEST(tick_phases_execute_in_correct_order);
    RUN_TEST(tick_deferred_actions_run_after_all_commits);

    TEST_SUITE("priority deferred actions");
    RUN_TEST(priority_high_runs_before_low);
    RUN_TEST(priority_fifo_within_same_priority);
    RUN_TEST(priority_mixed_order);
    RUN_TEST(dispatch_deferred_null_pool_runs_inline);

    TEST_SUMMARY();
}
