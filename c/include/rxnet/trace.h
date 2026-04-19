/* rxnet/trace.h — optional, zero-overhead tracing subsystem.
 *
 * Zero overhead when RX_TRACE_ENABLE is not defined
 * --------------------------------------------------
 * All public macros expand to ((void)0).  rx_node gains no extra fields.
 * trace.c contributes no symbols.  There is no runtime cost, no branch,
 * no pointer.
 *
 * When RX_TRACE_ENABLE is defined
 * --------------------------------
 * rx_node gains two fields: trace (pointer) and trace_nid (node id).
 * rx_tick() emits NODE_START / NODE_END (and phase events if phases=1).
 * rx_fsm_machine_commit() emits FSM_TRANSITION when the state changes.
 * rx_pn_net_commit() emits PN_FIRING for every transition that fires.
 *
 * Quick start
 * -----------
 *   rx_trace_buf_t tracer;
 *   rx_trace_init(&tracer, 0);                          // phases=0 (off)
 *   rx_trace_attach(&tracer, &machine.node, 0);         // node index 0
 *   rx_trace_set_node_name(&tracer, 0, "light");
 *   rx_trace_set_state_name(&tracer, 0, LIGHT_OFF, "OFF");
 *   rx_trace_set_state_name(&tracer, 0, LIGHT_ON,  "ON");
 *   // ... run system ...
 *   rx_trace_export(&tracer, "trace.bin");
 *   // python -m rxnet.tools.trace trace.bin --report trace.html --open
 *
 * Overridable platform hooks (define before including this header)
 * ---------------------------------------------------------------
 *   RX_TRACE_NOW_NS()                    — monotonic nanosecond clock
 *   RX_TRACE_LOCK_TYPE                   — lock type embedded in the buffer
 *   RX_TRACE_LOCK_INIT(lock)             — initialise the lock
 *   RX_TRACE_LOCK_ACQUIRE(lock)          — enter critical section
 *   RX_TRACE_LOCK_RELEASE(lock)          — exit critical section
 */
#pragma once

/* ── configurable limits (override via -D before including) ────────────── */
#ifndef RX_TRACE_MAX_EVENTS
#  define RX_TRACE_MAX_EVENTS  512u
#endif
#ifndef RX_TRACE_MAX_NODES
#  define RX_TRACE_MAX_NODES   8u
#endif
#ifndef RX_TRACE_MAX_STATES
#  define RX_TRACE_MAX_STATES  8u   /* FSM states per node */
#endif
#ifndef RX_TRACE_MAX_PLACES
#  define RX_TRACE_MAX_PLACES  8u   /* PN places per node  */
#endif
#ifndef RX_TRACE_MAX_TRANS
#  define RX_TRACE_MAX_TRANS   8u   /* PN transitions per node */
#endif
#ifndef RX_TRACE_MAX_LABELS
#  define RX_TRACE_MAX_LABELS  8u   /* user event labels */
#endif
#ifndef RX_TRACE_NAME_LEN
#  define RX_TRACE_NAME_LEN   24u   /* max bytes per name (incl. NUL) */
#endif

/* ── event kind constants ───────────────────────────────────────────────── */
#define RX_TRACE_EV_TICK     0u
#define RX_TRACE_EV_N_START  1u
#define RX_TRACE_EV_N_END    2u
#define RX_TRACE_EV_PH_START 3u
#define RX_TRACE_EV_PH_END   4u
#define RX_TRACE_EV_FSM      5u
#define RX_TRACE_EV_PN       6u
#define RX_TRACE_EV_USER     7u

/* ── phase id constants ─────────────────────────────────────────────────── */
#define RX_TRACE_PH_LATCH  0u
#define RX_TRACE_PH_EVAL   1u
#define RX_TRACE_PH_COMMIT 2u
#define RX_TRACE_PH_DUMP   3u

#ifdef RX_TRACE_ENABLE

#include <stddef.h>
#include <stdint.h>

/* ── default POSIX platform hooks ─────────────────────────────────────── */

#ifndef RX_TRACE_NOW_NS
#  include <time.h>
static inline uint64_t _rx_trace_now_ns(void) {
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    return (uint64_t)_ts.tv_sec * 1000000000ULL + (uint64_t)_ts.tv_nsec;
}
#  define RX_TRACE_NOW_NS() _rx_trace_now_ns()
#endif

#ifndef RX_TRACE_LOCK_TYPE
#  include <pthread.h>
#  define RX_TRACE_LOCK_TYPE            pthread_mutex_t
#  define RX_TRACE_LOCK_INIT(lk)        pthread_mutex_init(&(lk), NULL)
#  define RX_TRACE_LOCK_ACQUIRE(lk)     pthread_mutex_lock(&(lk))
#  define RX_TRACE_LOCK_RELEASE(lk)     pthread_mutex_unlock(&(lk))
#endif

/* ── trace buffer ─────────────────────────────────────────────────────── */

typedef struct rx_trace_buf rx_trace_buf_t;

struct rx_trace_buf {
    /* ring buffer */
    uint8_t  ev[RX_TRACE_MAX_EVENTS * 16u]; /* 16 bytes per event        */
    uint32_t head;    /* absolute next-write index (never wraps)           */
    uint32_t n;       /* events currently stored (≤ RX_TRACE_MAX_EVENTS)  */
    uint32_t dropped; /* events lost due to overflow                       */
    uint64_t t0_ns;   /* epoch: time of rx_trace_init()                   */
    uint8_t  phases;  /* record phase-boundary events?                     */
    uint8_t  _pad[3];
    RX_TRACE_LOCK_TYPE lock;

    /* name tables (populated via rx_trace_set_*) */
    uint8_t node_count;
    char    node_names[RX_TRACE_MAX_NODES][RX_TRACE_NAME_LEN];

    uint8_t fsm_state_count[RX_TRACE_MAX_NODES];
    char    fsm_state_names[RX_TRACE_MAX_NODES][RX_TRACE_MAX_STATES][RX_TRACE_NAME_LEN];

    uint8_t pn_place_count[RX_TRACE_MAX_NODES];
    char    pn_place_names[RX_TRACE_MAX_NODES][RX_TRACE_MAX_PLACES][RX_TRACE_NAME_LEN];

    uint8_t pn_trans_count[RX_TRACE_MAX_NODES];
    char    pn_trans_names[RX_TRACE_MAX_NODES][RX_TRACE_MAX_TRANS][RX_TRACE_NAME_LEN];

    uint8_t label_count;
    char    label_names[RX_TRACE_MAX_LABELS][RX_TRACE_NAME_LEN];
};

/* ── forward declaration to avoid circular include ────────────────────── */
struct rx_node; /* defined in rxnet/runtime.h */

/* ── public API ───────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the buffer.  phases=1 records phase-boundary events. */
void rx_trace_init(rx_trace_buf_t *buf, uint8_t phases);

/* Attach the buffer to a node: sets node->trace and node->trace_nid. */
void rx_trace_attach(rx_trace_buf_t *buf, struct rx_node *node, uint8_t nid);

/* Name registration — call once during setup. */
void rx_trace_set_node_name (rx_trace_buf_t *buf, uint8_t nid, const char *name);
void rx_trace_set_state_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t sid, const char *name);
void rx_trace_set_place_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t pid, const char *name);
void rx_trace_set_trans_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t tid, const char *name);
void rx_trace_set_label_name(rx_trace_buf_t *buf, uint8_t lid, const char *name);

/* Inject a user-defined event (thread-safe). */
void rx_trace_user(rx_trace_buf_t *buf, uint8_t lid, uint16_t value);

/* Export the complete binary blob (header + name table + events) to path. */
int  rx_trace_export(rx_trace_buf_t *buf, const char *path);

/* Internal write — called by the macros below; do not call directly. */
void rx_trace_write(rx_trace_buf_t *buf, uint8_t kind, uint8_t nid,
                    uint16_t a, uint16_t b, uint16_t c);

#ifdef __cplusplus
}
#endif

/* ── hot-path macros (only called when trace field is non-NULL) ───────── */

#define RX_TRACE_NODE_START(node) do { \
    if ((node)->trace) \
        rx_trace_write((node)->trace, RX_TRACE_EV_N_START, (node)->trace_nid, 0, 0, 0); \
} while (0)

#define RX_TRACE_NODE_END(node) do { \
    if ((node)->trace) \
        rx_trace_write((node)->trace, RX_TRACE_EV_N_END, (node)->trace_nid, 0, 0, 0); \
} while (0)

#define RX_TRACE_FSM(node, from, to) do { \
    if ((node)->trace && (int)(from) != (int)(to)) \
        rx_trace_write((node)->trace, RX_TRACE_EV_FSM, (node)->trace_nid, \
                       (uint16_t)(from), (uint16_t)(to), 0); \
} while (0)

#define RX_TRACE_PN(node, tidx) do { \
    if ((node)->trace) \
        rx_trace_write((node)->trace, RX_TRACE_EV_PN, (node)->trace_nid, \
                       (uint16_t)(tidx), 0, 0); \
} while (0)

#define RX_TRACE_PH_START(node, ph) do { \
    if ((node)->trace && (node)->trace->phases) \
        rx_trace_write((node)->trace, RX_TRACE_EV_PH_START, (node)->trace_nid, \
                       (uint16_t)(ph), 0, 0); \
} while (0)

#define RX_TRACE_PH_END(node, ph) do { \
    if ((node)->trace && (node)->trace->phases) \
        rx_trace_write((node)->trace, RX_TRACE_EV_PH_END, (node)->trace_nid, \
                       (uint16_t)(ph), 0, 0); \
} while (0)

#else /* ── RX_TRACE_ENABLE not defined: all macros are no-ops ─────────── */

#define RX_TRACE_NODE_START(node)         ((void)0)
#define RX_TRACE_NODE_END(node)           ((void)0)
#define RX_TRACE_FSM(node, from, to)      ((void)0)
#define RX_TRACE_PN(node, tidx)           ((void)0)
#define RX_TRACE_PH_START(node, ph)       ((void)0)
#define RX_TRACE_PH_END(node, ph)         ((void)0)

#endif /* RX_TRACE_ENABLE */
