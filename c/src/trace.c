// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/* rxnet/trace.c — optional tracing subsystem implementation.
 *
 * Compiled only when RX_TRACE_ENABLE is defined.  The Makefile passes
 * -DRX_TRACE_ENABLE only for trace-enabled targets; regular library builds
 * do not include this file.
 */
#ifdef RX_TRACE_ENABLE

#include "rxnet/trace.h"
#include "rxnet/runtime.h"

#include <stdio.h>
#include <string.h>

/* ── binary wire-format constants (shared with Python decoder) ─────────── */
#define _MAGIC    "RXNT"
#define _VERSION  1u
#define _EV_SIZE  16u
#define _LANG_C   0x43u  /* 'C' */

/* ── internal helpers ──────────────────────────────────────────────────── */

/* Write one little-endian uint16 into dst. */
static void _u16le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)(v >> 8u);
}

/* Write one little-endian uint32 into dst. */
static void _u32le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v       & 0xFFu);
    dst[1] = (uint8_t)((v>> 8u)& 0xFFu);
    dst[2] = (uint8_t)((v>>16u)& 0xFFu);
    dst[3] = (uint8_t)((v>>24u)& 0xFFu);
}

/* Write one little-endian uint64 into dst. */
static void _u64le(uint8_t *dst, uint64_t v) {
    _u32le(dst,     (uint32_t)(v       & 0xFFFFFFFFULL));
    _u32le(dst + 4, (uint32_t)(v >> 32u));
}

/* Copy up to (RX_TRACE_NAME_LEN-1) chars from src, always NUL-terminate. */
static void _safe_strcpy(char *dst, const char *src) {
    size_t i;
    for (i = 0; i < RX_TRACE_NAME_LEN - 1u && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Write one byte to file. */
static void _wbyte(FILE *f, uint8_t b) { fwrite(&b, 1, 1, f); }

/* Write a length-prefixed name (1-byte length + bytes, no NUL). */
static void _wname(FILE *f, const char *name) {
    uint8_t len = (uint8_t)strlen(name);
    _wbyte(f, len);
    fwrite(name, 1, len, f);
}

/* ── public API ────────────────────────────────────────────────────────── */

static int _find_attached_nid(const rx_trace_buf_t *buf, const struct rx_node *node) {
    uint8_t nid;

    if (!buf || !node) return -1;
    for (nid = 0; nid < buf->node_count; ++nid) {
        if (buf->attached_nodes[nid] == node)
            return (int)nid;
    }
    return -1;
}

void rx_trace_init(rx_trace_buf_t *buf, uint8_t phases) {
    if (!buf) return;
    memset(buf, 0, sizeof(*buf));
    buf->phases = phases;
    buf->t0_ns  = RX_TRACE_NOW_NS();
    RX_TRACE_LOCK_INIT(buf->lock);
}

void rx_trace_attach(rx_trace_buf_t *buf, struct rx_node *node, uint8_t nid) {
    int prev_nid;

    if (!buf || !node) return;
    if (nid >= RX_TRACE_MAX_NODES) return;

    prev_nid = _find_attached_nid(buf, node);
    if (prev_nid >= 0 && (uint8_t)prev_nid != nid)
        buf->attached_nodes[prev_nid] = NULL;

    node->trace     = buf;
    node->trace_nid = nid;
    buf->attached_nodes[nid] = node;
    if (nid >= buf->node_count)
        buf->node_count = (uint8_t)(nid + 1u);
}

int rx_trace_attach_runtime(rx_trace_buf_t *buf, struct rx_runtime *rt) {
    size_t i;

    if (!buf || !rt) return -1;

    for (i = 0; i < rt->node_count; ++i) {
        rx_node *node = rt->nodes[i].node;
        int nid;

        if (!node) continue;

        if (node->trace == buf && node->trace_nid < RX_TRACE_MAX_NODES) {
            if (buf->attached_nodes[node->trace_nid] == NULL)
                buf->attached_nodes[node->trace_nid] = node;
            if (node->trace_nid >= buf->node_count)
                buf->node_count = (uint8_t)(node->trace_nid + 1u);
            continue;
        }

        nid = _find_attached_nid(buf, node);
        if (nid < 0) {
            if (buf->node_count >= RX_TRACE_MAX_NODES)
                return -1;
            nid = (int)buf->node_count;
        }

        rx_trace_attach(buf, node, (uint8_t)nid);
    }

    return 0;
}

void rx_trace_set_node_name(rx_trace_buf_t *buf, uint8_t nid, const char *name) {
    if (!buf || !name || nid >= RX_TRACE_MAX_NODES) return;
    _safe_strcpy(buf->node_names[nid], name);
    if (nid >= buf->node_count)
        buf->node_count = (uint8_t)(nid + 1u);
}

void rx_trace_set_state_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t sid, const char *name) {
    if (!buf || !name || nid >= RX_TRACE_MAX_NODES || sid >= RX_TRACE_MAX_STATES) return;
    _safe_strcpy(buf->fsm_state_names[nid][sid], name);
    if (sid >= buf->fsm_state_count[nid])
        buf->fsm_state_count[nid] = (uint8_t)(sid + 1u);
}

void rx_trace_set_place_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t pid, const char *name) {
    if (!buf || !name || nid >= RX_TRACE_MAX_NODES || pid >= RX_TRACE_MAX_PLACES) return;
    _safe_strcpy(buf->pn_place_names[nid][pid], name);
    if (pid >= buf->pn_place_count[nid])
        buf->pn_place_count[nid] = (uint8_t)(pid + 1u);
}

void rx_trace_set_trans_name(rx_trace_buf_t *buf, uint8_t nid, uint8_t tid, const char *name) {
    if (!buf || !name || nid >= RX_TRACE_MAX_NODES || tid >= RX_TRACE_MAX_TRANS) return;
    _safe_strcpy(buf->pn_trans_names[nid][tid], name);
    if (tid >= buf->pn_trans_count[nid])
        buf->pn_trans_count[nid] = (uint8_t)(tid + 1u);
}

void rx_trace_set_label_name(rx_trace_buf_t *buf, uint8_t lid, const char *name) {
    if (!buf || !name || lid >= RX_TRACE_MAX_LABELS) return;
    _safe_strcpy(buf->label_names[lid], name);
    if (lid >= buf->label_count)
        buf->label_count = (uint8_t)(lid + 1u);
}

void rx_trace_user(rx_trace_buf_t *buf, uint8_t lid, uint16_t value) {
    if (!buf) return;
    rx_trace_write(buf, RX_TRACE_EV_USER, 0, (uint16_t)lid, value, 0);
}

/* ── ring buffer write (called from hot-path macros) ───────────────────── */

void rx_trace_write(rx_trace_buf_t *buf, uint8_t kind, uint8_t nid,
                    uint16_t a, uint16_t b, uint16_t c) {
    uint64_t t;
    uint32_t slot;
    uint8_t *ev;

    t = RX_TRACE_NOW_NS() - buf->t0_ns;

    RX_TRACE_LOCK_ACQUIRE(buf->lock);

    slot = buf->head % RX_TRACE_MAX_EVENTS;
    ev   = buf->ev + slot * 16u;

    _u64le(ev,      t);
    ev[8]  = kind;
    ev[9]  = nid;
    _u16le(ev + 10, a);
    _u16le(ev + 12, b);
    _u16le(ev + 14, c);

    buf->head++;
    if (buf->n < RX_TRACE_MAX_EVENTS)
        buf->n++;
    else
        buf->dropped++;

    RX_TRACE_LOCK_RELEASE(buf->lock);
}

/* ── binary export ─────────────────────────────────────────────────────── */

int rx_trace_export(rx_trace_buf_t *buf, const char *path) {
    FILE     *f;
    uint8_t   hdr[32];
    uint32_t  n, dropped, head;
    uint64_t  t0;
    uint32_t  start, first_part, second_part;
    uint8_t   flags;
    uint8_t   i, j;

    if (!buf || !path) return -1;

    f = fopen(path, "wb");
    if (!f) return -1;

    /* Snapshot under lock */
    RX_TRACE_LOCK_ACQUIRE(buf->lock);
    n       = buf->n;
    dropped = buf->dropped;
    head    = buf->head;
    t0      = buf->t0_ns;
    RX_TRACE_LOCK_RELEASE(buf->lock);

    /* ── 32-byte header ── */
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'R'; hdr[1] = 'X'; hdr[2] = 'N'; hdr[3] = 'T';
    hdr[4] = (uint8_t)_VERSION;
    hdr[5] = (uint8_t)_EV_SIZE;
    hdr[6] = (uint8_t)_LANG_C;
    flags  = (buf->phases ? 0x01u : 0x00u) | 0x02u; /* bit1 = has_names */
    hdr[7] = flags;
    _u64le(hdr + 8,  t0);
    _u32le(hdr + 16, n);
    _u32le(hdr + 20, dropped);
    /* hdr[24..31] = reserved zeros */
    fwrite(hdr, 1, 32, f);

    /* ── name table ── */

    /* node names */
    _wbyte(f, buf->node_count);
    for (i = 0; i < buf->node_count; ++i)
        _wname(f, buf->node_names[i]);

    /* FSM state names — emit only nodes that have states registered */
    {
        uint8_t fc = 0;
        for (i = 0; i < buf->node_count; ++i)
            if (buf->fsm_state_count[i] > 0) fc++;
        _wbyte(f, fc);
        for (i = 0; i < buf->node_count; ++i) {
            uint8_t sc = buf->fsm_state_count[i];
            if (sc == 0) continue;
            _wbyte(f, i);  /* nid */
            _wbyte(f, sc); /* state count */
            for (j = 0; j < sc; ++j) {
                _wbyte(f, j); /* sid */
                _wname(f, buf->fsm_state_names[i][j]);
            }
        }
    }

    /* PN place + transition names — emit only nodes that have any */
    {
        uint8_t pc = 0;
        for (i = 0; i < buf->node_count; ++i)
            if (buf->pn_place_count[i] > 0 || buf->pn_trans_count[i] > 0) pc++;
        _wbyte(f, pc);
        for (i = 0; i < buf->node_count; ++i) {
            uint8_t np = buf->pn_place_count[i];
            uint8_t nt = buf->pn_trans_count[i];
            if (np == 0 && nt == 0) continue;
            _wbyte(f, i);  /* nid */
            _wbyte(f, np);
            _wbyte(f, nt);
            for (j = 0; j < np; ++j) {
                _wbyte(f, j);
                _wname(f, buf->pn_place_names[i][j]);
            }
            for (j = 0; j < nt; ++j) {
                _wbyte(f, j);
                _wname(f, buf->pn_trans_names[i][j]);
            }
        }
    }

    /* user labels */
    _wbyte(f, buf->label_count);
    for (i = 0; i < buf->label_count; ++i) {
        _wbyte(f, i);
        _wname(f, buf->label_names[i]);
    }

    /* ── events in time order ── */
    if (n > 0) {
        start = (head - n) % RX_TRACE_MAX_EVENTS;
        if (start + n <= RX_TRACE_MAX_EVENTS) {
            fwrite(buf->ev + start * 16u, 16u, n, f);
        } else {
            first_part  = RX_TRACE_MAX_EVENTS - start;
            second_part = n - first_part;
            fwrite(buf->ev + start * 16u, 16u, first_part,  f);
            fwrite(buf->ev,               16u, second_part, f);
        }
    }

    fclose(f);
    return 0;
}

#endif /* RX_TRACE_ENABLE */
