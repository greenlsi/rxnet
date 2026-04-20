// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

/* test_trace.c — unit tests for rxnet/trace.h + trace.c.
 *
 * Compiled with -DRX_TRACE_ENABLE.
 */
#include "rxtest.h"

#ifndef RX_TRACE_ENABLE
#define RX_TRACE_ENABLE
#endif
#include "rxnet/trace.h"
#include "rxnet/fsm.h"
#include "rxnet/pn.h"

#include <string.h>
#include <stdio.h>

/* ── helpers ─────────────────────────────────────────────────────────── */

static void noop_fsm_latch(rx_fsm_context *ctx, void *user) { (void)ctx; (void)user; }
static void noop_fsm_dump (rx_fsm_context *ctx, void *user) { (void)ctx; (void)user; }
static void noop_pn_latch (rx_pn_context  *ctx, void *user) { (void)ctx; (void)user; }
static void noop_pn_dump  (rx_pn_context  *ctx, void *user) { (void)ctx; (void)user; }

enum { OFF = 0, ON = 1 };

static int flag_val = 0;
static int guard_flag(const rx_fsm_context *ctx, void *user) {
    (void)ctx; (void)user;
    return flag_val;
}

/* Two-state FSM: OFF→ON when flag_val, ON→OFF always. */
static void make_light_machine(rx_fsm_runtime *rt, rx_fsm_machine *m) {
    static const rx_fsm_transition trans[] = {
        {OFF, ON,  guard_flag, NULL},
        {ON,  OFF, NULL,       NULL},
    };
    rx_fsm_runtime_init(rt, 1);
    rx_fsm_machine_init(m, "light", OFF, trans, 2, NULL,
                        noop_fsm_latch, noop_fsm_dump);
    rx_fsm_runtime_add_machine(rt, m, 0, 0);
}

/* One-transition PN: P0→T0→P1. */
static const rx_pn_arc pn_consume[] = {{ 0, 1 }};
static const rx_pn_arc pn_produce[] = {{ 1, 1 }};
static const rx_pn_transition pn_trans[] = {
    { pn_consume, 1, pn_produce, 1, NULL, NULL }
};
static void make_flow_net(rx_pn_runtime *rt, rx_pn_net *net) {
    static const int places[] = { 1, 0 };
    rx_pn_runtime_init(rt, 1);
    rx_pn_net_init(net, "flow", places, 2, pn_trans, 1, NULL,
                   noop_pn_latch, noop_pn_dump);
    rx_pn_runtime_add_net(rt, net, 0, 0);
}

/* Read a uint8 from a 16-byte event record at byte offset. */
static uint8_t ev_u8(const uint8_t *ev, int off) { return ev[off]; }

/* Read a uint16 little-endian from a 16-byte event record. */
static uint16_t ev_u16(const uint8_t *ev, int off) {
    return (uint16_t)(ev[off] | ((uint16_t)ev[off+1] << 8));
}

/* ── ring-buffer tests ───────────────────────────────────────────────── */

static void test_init_sets_zero_events(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    ASSERT_EQ(0, (int)buf.n);
    ASSERT_EQ(0, (int)buf.dropped);
}

static void test_write_increments_n(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    rx_trace_write(&buf, RX_TRACE_EV_N_START, 0, 0, 0, 0);
    ASSERT_EQ(1, (int)buf.n);
}

static void test_write_records_kind_and_nid(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    rx_trace_write(&buf, RX_TRACE_EV_FSM, 3, 10, 20, 0);
    /* first event is at slot 0 */
    ASSERT_EQ(RX_TRACE_EV_FSM, ev_u8(buf.ev, 8));
    ASSERT_EQ(3,                ev_u8(buf.ev, 9));
    ASSERT_EQ(10, ev_u16(buf.ev, 10));
    ASSERT_EQ(20, ev_u16(buf.ev, 12));
}

static void test_overflow_increments_dropped(void) {
    rx_trace_buf_t buf;
    size_t i;
    rx_trace_init(&buf, 0);
    for (i = 0; i <= RX_TRACE_MAX_EVENTS; ++i)
        rx_trace_write(&buf, 0, 0, 0, 0, 0);
    ASSERT_EQ((int)RX_TRACE_MAX_EVENTS, (int)buf.n);
    ASSERT_EQ(1, (int)buf.dropped);
}

static void test_overflow_keeps_newest(void) {
    rx_trace_buf_t buf;
    size_t i;
    uint8_t kind;
    rx_trace_init(&buf, 0);
    /* write CAP+1 events with kind = i (so kinds 0..CAP) */
    for (i = 0; i <= RX_TRACE_MAX_EVENTS; ++i)
        rx_trace_write(&buf, (uint8_t)(i % 8), 0, 0, 0, 0);
    /* oldest event (kind=0, which was i=0) was dropped;
     * the first stored event now has kind=(1%8)=1 */
    {
        uint32_t start = (buf.head - buf.n) % RX_TRACE_MAX_EVENTS;
        kind = ev_u8(buf.ev + start * 16u, 8);
        ASSERT_EQ(1, (int)kind);
    }
}

/* ── attach / name registration ─────────────────────────────────────── */

static void test_attach_sets_trace_pointer(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    rx_trace_init(&buf, 0);
    make_light_machine(&rt, &m);
    rx_trace_attach(&buf, &m.node, 0);
    ASSERT_TRUE(m.node.trace == &buf);
    ASSERT_EQ(0, (int)m.node.trace_nid);
    rx_fsm_runtime_free(&rt);
}

static void test_set_node_name_stored(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    rx_trace_set_node_name(&buf, 0, "light");
    ASSERT_TRUE(strcmp(buf.node_names[0], "light") == 0);
    ASSERT_EQ(1, (int)buf.node_count);
}

static void test_set_state_name_stored(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    rx_trace_set_state_name(&buf, 0, OFF, "OFF");
    rx_trace_set_state_name(&buf, 0, ON,  "ON");
    ASSERT_TRUE(strcmp(buf.fsm_state_names[0][OFF], "OFF") == 0);
    ASSERT_TRUE(strcmp(buf.fsm_state_names[0][ON],  "ON")  == 0);
    ASSERT_EQ(2, (int)buf.fsm_state_count[0]);
}

static void test_set_place_name_stored(void) {
    rx_trace_buf_t buf;
    rx_trace_init(&buf, 0);
    rx_trace_set_place_name(&buf, 0, 0, "SRC");
    rx_trace_set_place_name(&buf, 0, 1, "DST");
    ASSERT_TRUE(strcmp(buf.pn_place_names[0][0], "SRC") == 0);
    ASSERT_EQ(2, (int)buf.pn_place_count[0]);
}

/* ── FSM integration ─────────────────────────────────────────────────── */

static void test_fsm_node_start_end_recorded(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    uint8_t kinds[8];
    size_t i;

    rx_trace_init(&buf, 0);
    make_light_machine(&rt, &m);
    rx_trace_attach(&buf, &m.node, 0);

    flag_val = 0;
    rx_fsm_tick(&rt);

    ASSERT_TRUE(buf.n >= 2);
    for (i = 0; i < buf.n && i < 8; ++i)
        kinds[i] = ev_u8(buf.ev + i * 16u, 8);
    ASSERT_EQ(RX_TRACE_EV_N_START, (int)kinds[0]);
    ASSERT_EQ(RX_TRACE_EV_N_END,   (int)kinds[buf.n - 1]);

    rx_fsm_runtime_free(&rt);
}

static void test_fsm_no_transition_event_when_state_unchanged(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    size_t i;

    rx_trace_init(&buf, 0);
    make_light_machine(&rt, &m);   /* starts OFF */
    rx_trace_attach(&buf, &m.node, 0);

    flag_val = 0;                  /* guard fails: stays OFF */
    rx_fsm_tick(&rt);

    for (i = 0; i < buf.n; ++i)
        ASSERT_TRUE(ev_u8(buf.ev + i * 16u, 8) != RX_TRACE_EV_FSM);

    rx_fsm_runtime_free(&rt);
}

static void test_fsm_transition_event_recorded(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    size_t i;
    int found = 0;

    rx_trace_init(&buf, 0);
    make_light_machine(&rt, &m);
    rx_trace_attach(&buf, &m.node, 0);

    flag_val = 1;                  /* guard passes: OFF→ON */
    rx_fsm_tick(&rt);

    for (i = 0; i < buf.n; ++i) {
        const uint8_t *ev = buf.ev + i * 16u;
        if (ev_u8(ev, 8) == RX_TRACE_EV_FSM) {
            ASSERT_EQ(OFF, (int)ev_u16(ev, 10)); /* a = from */
            ASSERT_EQ(ON,  (int)ev_u16(ev, 12)); /* b = to   */
            found = 1;
        }
    }
    ASSERT_TRUE(found);

    rx_fsm_runtime_free(&rt);
}

/* ── PN integration ──────────────────────────────────────────────────── */

static void test_pn_firing_event_recorded(void) {
    rx_trace_buf_t buf;
    rx_pn_runtime rt;
    rx_pn_net net;
    size_t i;
    int found = 0;

    rx_trace_init(&buf, 0);
    make_flow_net(&rt, &net);       /* P0=1, P1=0 → T0 can fire */
    rx_trace_attach(&buf, &net.node, 0);

    rx_pn_tick(&rt);

    for (i = 0; i < buf.n; ++i) {
        const uint8_t *ev = buf.ev + i * 16u;
        if (ev_u8(ev, 8) == RX_TRACE_EV_PN) {
            ASSERT_EQ(0, (int)ev_u16(ev, 10)); /* a = transition index */
            found = 1;
        }
    }
    ASSERT_TRUE(found);

    rx_pn_net_free(&net);
    rx_pn_runtime_free(&rt);
}

static void test_pn_no_firing_event_second_tick(void) {
    rx_trace_buf_t buf;
    rx_pn_runtime rt;
    rx_pn_net net;
    size_t i;
    int count = 0;

    rx_trace_init(&buf, 0);
    make_flow_net(&rt, &net);
    rx_trace_attach(&buf, &net.node, 0);

    rx_pn_tick(&rt);   /* T0 fires (P0→P1) */
    rx_pn_tick(&rt);   /* T0 cannot fire (P0=0) */

    for (i = 0; i < buf.n; ++i)
        if (ev_u8(buf.ev + i * 16u, 8) == RX_TRACE_EV_PN) count++;

    ASSERT_EQ(1, count);  /* only one firing across two ticks */

    rx_pn_net_free(&net);
    rx_pn_runtime_free(&rt);
}

/* ── user events ─────────────────────────────────────────────────────── */

static void test_user_event_recorded(void) {
    rx_trace_buf_t buf;
    size_t i;
    int found = 0;

    rx_trace_init(&buf, 0);
    rx_trace_set_label_name(&buf, 0, "temp");
    rx_trace_user(&buf, 0, 42);

    for (i = 0; i < buf.n; ++i) {
        const uint8_t *ev = buf.ev + i * 16u;
        if (ev_u8(ev, 8) == RX_TRACE_EV_USER) {
            ASSERT_EQ(0,  (int)ev_u16(ev, 10)); /* lid */
            ASSERT_EQ(42, (int)ev_u16(ev, 12)); /* value */
            found = 1;
        }
    }
    ASSERT_TRUE(found);
}

/* ── binary export ───────────────────────────────────────────────────── */

static void test_export_writes_magic(void) {
    rx_trace_buf_t buf;
    FILE *f;
    uint8_t hdr[4];

    rx_trace_init(&buf, 0);
    rx_trace_write(&buf, RX_TRACE_EV_N_START, 0, 0, 0, 0);
    ASSERT_EQ(0, rx_trace_export(&buf, "/tmp/rxnet_test.bin"));

    f = fopen("/tmp/rxnet_test.bin", "rb");
    ASSERT_NOT_NULL(f);
    fread(hdr, 1, 4, f);
    fclose(f);
    ASSERT_EQ('R', hdr[0]);
    ASSERT_EQ('X', hdr[1]);
    ASSERT_EQ('N', hdr[2]);
    ASSERT_EQ('T', hdr[3]);
    remove("/tmp/rxnet_test.bin");
}

static void test_export_has_names_flag(void) {
    rx_trace_buf_t buf;
    FILE *f;
    uint8_t hdr[8];

    rx_trace_init(&buf, 0);
    ASSERT_EQ(0, rx_trace_export(&buf, "/tmp/rxnet_test2.bin"));

    f = fopen("/tmp/rxnet_test2.bin", "rb");
    ASSERT_NOT_NULL(f);
    fread(hdr, 1, 8, f);
    fclose(f);
    ASSERT_TRUE(hdr[7] & 0x02u);  /* bit1 = has_names */
    remove("/tmp/rxnet_test2.bin");
}

static void test_export_lang_byte_is_C(void) {
    rx_trace_buf_t buf;
    FILE *f;
    uint8_t hdr[8];

    rx_trace_init(&buf, 0);
    ASSERT_EQ(0, rx_trace_export(&buf, "/tmp/rxnet_test3.bin"));

    f = fopen("/tmp/rxnet_test3.bin", "rb");
    ASSERT_NOT_NULL(f);
    fread(hdr, 1, 8, f);
    fclose(f);
    ASSERT_EQ('C', (int)hdr[6]);
    remove("/tmp/rxnet_test3.bin");
}

static void test_export_event_count_in_header(void) {
    rx_trace_buf_t buf;
    FILE *f;
    uint8_t hdr[32];
    uint32_t n;

    rx_trace_init(&buf, 0);
    rx_trace_write(&buf, 0, 0, 0, 0, 0);
    rx_trace_write(&buf, 0, 0, 0, 0, 0);
    rx_trace_write(&buf, 0, 0, 0, 0, 0);
    ASSERT_EQ(0, rx_trace_export(&buf, "/tmp/rxnet_test4.bin"));

    f = fopen("/tmp/rxnet_test4.bin", "rb");
    ASSERT_NOT_NULL(f);
    fread(hdr, 1, 32, f);
    fclose(f);
    n = (uint32_t)(hdr[16] | ((uint32_t)hdr[17]<<8) |
                   ((uint32_t)hdr[18]<<16) | ((uint32_t)hdr[19]<<24));
    ASSERT_EQ(3, (int)n);
    remove("/tmp/rxnet_test4.bin");
}

/* ── phase events ────────────────────────────────────────────────────── */

static void test_phase_events_when_phases_on(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    size_t i;
    int found_ph_start = 0;

    rx_trace_init(&buf, 1);       /* phases = 1 */
    make_light_machine(&rt, &m);
    rx_trace_attach(&buf, &m.node, 0);
    flag_val = 0;
    rx_fsm_tick(&rt);

    for (i = 0; i < buf.n; ++i)
        if (ev_u8(buf.ev + i * 16u, 8) == RX_TRACE_EV_PH_START) found_ph_start = 1;

    ASSERT_TRUE(found_ph_start);
    rx_fsm_runtime_free(&rt);
}

static void test_no_phase_events_when_phases_off(void) {
    rx_trace_buf_t buf;
    rx_fsm_runtime rt;
    rx_fsm_machine m;
    size_t i;

    rx_trace_init(&buf, 0);       /* phases = 0 */
    make_light_machine(&rt, &m);
    rx_trace_attach(&buf, &m.node, 0);
    flag_val = 0;
    rx_fsm_tick(&rt);

    for (i = 0; i < buf.n; ++i)
        ASSERT_TRUE(ev_u8(buf.ev + i * 16u, 8) != RX_TRACE_EV_PH_START);

    rx_fsm_runtime_free(&rt);
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void) {
    TEST_SUITE("ring buffer");
    RUN_TEST(test_init_sets_zero_events);
    RUN_TEST(test_write_increments_n);
    RUN_TEST(test_write_records_kind_and_nid);
    RUN_TEST(test_overflow_increments_dropped);
    RUN_TEST(test_overflow_keeps_newest);

    TEST_SUITE("attach / name registration");
    RUN_TEST(test_attach_sets_trace_pointer);
    RUN_TEST(test_set_node_name_stored);
    RUN_TEST(test_set_state_name_stored);
    RUN_TEST(test_set_place_name_stored);

    TEST_SUITE("FSM integration");
    RUN_TEST(test_fsm_node_start_end_recorded);
    RUN_TEST(test_fsm_no_transition_event_when_state_unchanged);
    RUN_TEST(test_fsm_transition_event_recorded);

    TEST_SUITE("PN integration");
    RUN_TEST(test_pn_firing_event_recorded);
    RUN_TEST(test_pn_no_firing_event_second_tick);

    TEST_SUITE("user events");
    RUN_TEST(test_user_event_recorded);

    TEST_SUITE("binary export");
    RUN_TEST(test_export_writes_magic);
    RUN_TEST(test_export_has_names_flag);
    RUN_TEST(test_export_lang_byte_is_C);
    RUN_TEST(test_export_event_count_in_header);

    TEST_SUITE("phase events");
    RUN_TEST(test_phase_events_when_phases_on);
    RUN_TEST(test_no_phase_events_when_phases_off);

    TEST_SUMMARY();
}
