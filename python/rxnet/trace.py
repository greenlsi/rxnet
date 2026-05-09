# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet.trace — optional, zero-overhead tracing subsystem.

Zero overhead when not in use
------------------------------
``Runtime``, ``Machine``, and ``Net`` contain no tracer-related code.
The tracer operates exclusively through transparent ``_Traced`` node
wrappers inserted into ``runtime._entries`` by ``Tracer.attach()``.
If ``attach()`` is never called, the production code path is identical
to a build that has never heard of tracing.

Quick start::

    from rxnet.trace import Tracer

    tracer = Tracer(max_events=4096)
    tracer.attach(rt)           # call before ce.run() / te.run()
    tracer.serve(port=7777)     # HTTP daemon; GET /trace returns binary blob

    ce = CyclicExecutive()
    ce.add(rt)
    ce.run()

On the dev machine::

    python -m rxnet.tools.trace http://target:7777 --report report.html --open
"""
from __future__ import annotations

import http.server
import struct
import threading
import time
from collections.abc import Callable
from typing import Any

# ── event kinds ────────────────────────────────────────────────────────────
_EV_TICK     = 0   # slot activated
_EV_N_START  = 1   # node activation begins (latch start)
_EV_N_END    = 2   # node completion (dump end)
_EV_PH_START = 3   # phase boundary start  (phases=True only)
_EV_PH_END   = 4   # phase boundary end    (phases=True only)
_EV_FSM      = 5   # FSM transition: a=from_state  b=to_state
_EV_PN       = 6   # PN firing:      a=transition_index
_EV_USER     = 7   # user event:     a=label_id  b=value

# phase ids (used in a field of PHASE_START / PHASE_END)
_PH_LATCH  = 0
_PH_EVAL   = 1
_PH_COMMIT = 2
_PH_DUMP   = 3

# wire format: 8 + 1 + 1 + 2 + 2 + 2 = 16 bytes per event
_PACK = struct.Struct("<QBBHHH")


# ── ring buffer ─────────────────────────────────────────────────────────────

class _RingBuf:
    """Pre-allocated, thread-safe ring buffer.  One lock, one pack_into per write."""

    __slots__ = ("_data", "_cap", "_head", "_n", "_dropped", "_t0", "_lock")

    def __init__(self, capacity: int) -> None:
        self._cap     = capacity
        self._data    = bytearray(capacity * 16)
        self._head    = 0   # absolute write index (never wraps)
        self._n       = 0   # events stored (≤ capacity)
        self._dropped = 0
        self._t0      = time.monotonic_ns()
        self._lock    = threading.Lock()

    def write(self, kind: int, nid: int,
              a: int = 0, b: int = 0, c: int = 0) -> None:
        t = time.monotonic_ns() - self._t0
        with self._lock:
            _PACK.pack_into(self._data, (self._head % self._cap) * 16,
                            t, kind, nid, a, b, c)
            self._head += 1
            if self._n < self._cap:
                self._n += 1
            else:
                self._dropped += 1

    def drain(self) -> tuple[bytes, int, int, int]:
        """Return ``(events_in_order, count, dropped, t0_ns)`` under lock."""
        with self._lock:
            n, dropped, head, t0 = self._n, self._dropped, self._head, self._t0
            start = (head - n) % self._cap
            if n == 0:
                return b"", 0, dropped, t0
            if start + n <= self._cap:
                return bytes(self._data[start * 16:(start + n) * 16]), n, dropped, t0
            split = self._cap - start
            return (bytes(self._data[start * 16:])
                    + bytes(self._data[:(n - split) * 16])), n, dropped, t0


# ── node wrapper ─────────────────────────────────────────────────────────────

class _Traced:
    """Transparent wrapper around any Node.

    Delegates all four phases to the wrapped node and records timing /
    state-delta events into the shared ring buffer.

    ``_snap_fn`` and ``_delta_fn`` are bound once at construction — no
    ``isinstance`` checks in the hot path.
    """

    __slots__ = ("_inner", "_nid", "_buf", "_phases", "_snap_fn", "_delta_fn")
    # Declared here so mypy accepts heterogeneous assignments in __init__.
    _snap_fn:  Callable[[], Any]
    _delta_fn: Callable[[Any], None]

    def __init__(self, inner: Any, nid: int, buf: _RingBuf, phases: bool) -> None:
        self._inner  = inner
        self._nid    = nid
        self._buf    = buf
        self._phases = phases

        # Bind state-snapshot and delta functions at wrap time.
        from .fsm import Machine
        from .pn import Net
        if isinstance(inner, Machine):
            self._snap_fn  = lambda: inner.state
            self._delta_fn = self._fsm_delta
        elif isinstance(inner, Net):
            self._snap_fn  = lambda: None
            self._delta_fn = self._pn_delta
        else:
            self._snap_fn  = lambda: None
            self._delta_fn = lambda _prev: None

    # ── four phases ──────────────────────────────────────────────────────

    def latch_inputs(self, ctx: Any) -> None:
        buf, nid = self._buf, self._nid
        buf.write(_EV_N_START, nid)
        if self._phases:
            buf.write(_EV_PH_START, nid, a=_PH_LATCH)
        self._inner.latch_inputs(ctx)
        if self._phases:
            buf.write(_EV_PH_END, nid, a=_PH_LATCH)

    def evaluate(self, ctx: Any) -> None:
        buf, nid = self._buf, self._nid
        if self._phases:
            buf.write(_EV_PH_START, nid, a=_PH_EVAL)
        self._inner.evaluate(ctx)
        if self._phases:
            buf.write(_EV_PH_END, nid, a=_PH_EVAL)

    def commit(self, ctx: Any) -> None:
        buf, nid = self._buf, self._nid
        if self._phases:
            buf.write(_EV_PH_START, nid, a=_PH_COMMIT)
        prev = self._snap_fn()
        self._inner.commit(ctx)
        if self._phases:
            buf.write(_EV_PH_END, nid, a=_PH_COMMIT)
        self._delta_fn(prev)

    def dump_outputs(self, ctx: Any) -> None:
        buf, nid = self._buf, self._nid
        if self._phases:
            buf.write(_EV_PH_START, nid, a=_PH_DUMP)
        self._inner.dump_outputs(ctx)
        if self._phases:
            buf.write(_EV_PH_END, nid, a=_PH_DUMP)
        buf.write(_EV_N_END, nid)

    # ── delta detectors (bound at construction) ──────────────────────────

    def _fsm_delta(self, prev: int) -> None:
        curr = self._inner.state
        if curr != prev:
            self._buf.write(_EV_FSM, self._nid, a=prev, b=curr)

    def _pn_delta(self, _prev: Any) -> None:
        buf, nid = self._buf, self._nid
        for i, fired in enumerate(self._inner._fire_flags):
            if fired:
                buf.write(_EV_PN, nid, a=i)


# ── tracer ───────────────────────────────────────────────────────────────────

# Wire-format magic + version
_MAGIC   = b"RXNT"
_VERSION = 1
_LANG_PY = ord("P")


class Tracer:
    """Optional tracing subsystem for rxnet runtimes.

    Attach to a runtime before calling ``run()``.  Detach to restore the
    production code path with zero overhead.
    """

    def __init__(self, max_events: int = 4096, phases: bool = False) -> None:
        self._buf    = _RingBuf(max_events)
        self._phases = phases
        # per-node metadata (populated at attach time)
        self._nodes:  list[Any]       = []   # original nodes for diagram generation
        self._node_ids: dict[int, int] = {}  # id(original node) -> stable nid
        self._names:  dict[int, str]  = {}   # nid → display name
        self._inits:  dict[int, int]  = {}   # nid → initial FSM state
        # user-event label registry
        self._labels: dict[str, int]  = {}
        self._server: threading.Thread | None = None

    # ── attach / detach ──────────────────────────────────────────────────

    @staticmethod
    def _core(rt: Any) -> Any:
        """Return the underlying CoreRuntime whether *rt* is a wrapper or not."""
        return rt._core if hasattr(rt, "_core") else rt

    def attach(self, rt: Any) -> Tracer:
        """Wrap all nodes in *rt* with tracing proxies.

        Idempotent for already traced nodes and incremental for new nodes
        added after previous ``attach()`` / ``detach()`` cycles.  Must be
        called **before** ``run()``.  Returns ``self`` for chaining.
        """
        from .fsm import Machine

        core = self._core(rt)
        if not core._built:
            core.build()
        for entry in core._entries:
            if isinstance(entry.node, _Traced):
                continue
            node = entry.node
            node_key = id(node)
            nid = self._node_ids.get(node_key)
            if nid is None:
                nid = len(self._nodes)
                self._node_ids[node_key] = nid
                self._nodes.append(node)
                self._names[nid] = getattr(node, "name", f"node_{nid}")
                if isinstance(node, Machine):
                    self._inits[nid] = node.state   # snapshot initial state
            entry.node = _Traced(node, nid, self._buf, self._phases)
        core.build()   # rebuild slot lists with wrapped nodes
        return self

    def detach(self, rt: Any) -> Tracer:
        """Unwrap all traced nodes, restoring the production code path."""
        core = self._core(rt)
        for entry in core._entries:
            if isinstance(entry.node, _Traced):
                entry.node = entry.node._inner
        core.build()
        return self

    # ── user events ──────────────────────────────────────────────────────

    def user(self, label: str, value: int = 0) -> None:
        """Record a user-defined event.  Thread-safe."""
        if label not in self._labels:
            self._labels[label] = len(self._labels)
        self._buf.write(_EV_USER, 0, a=self._labels[label], b=value & 0xFFFF)

    # ── export ───────────────────────────────────────────────────────────

    def export(self, path: str) -> None:
        """Write binary wire format to *path*."""
        with open(path, "wb") as f:
            f.write(self._blob())

    def report(self, path: str) -> None:
        """Generate a standalone HTML report with DOT diagrams.

        Opens with any browser.  No server or installation needed.
        """
        from .diagram import fsm_to_dot, pn_to_dot
        from .fsm import Machine
        from .pn import Net

        sections: list[str] = []
        for nid, node in enumerate(self._nodes):
            name = self._names[nid]
            if isinstance(node, Machine):
                dot = fsm_to_dot(node, initial_state=self._inits.get(nid))
                sections.append(_html_diagram(name, "FSM", dot))
            elif isinstance(node, Net):
                sections.append(_html_diagram(name, "Petri Net", pn_to_dot(node)))

        with open(path, "w", encoding="utf-8") as f:
            f.write(_HTML_TEMPLATE.format(
                title   = "rxnet trace",
                diagrams= "\n".join(sections),
            ))

    # ── HTTP server ───────────────────────────────────────────────────────

    def serve(self, host: str = "0.0.0.0", port: int = 7777) -> Tracer:
        """Start an HTTP daemon for live trace download.

        ``GET /trace``  → binary wire format
        ``GET /status`` → JSON summary
        Returns ``self`` for chaining.
        """
        tracer = self

        class _Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                if self.path in ("/trace", "/trace.bin"):
                    body = tracer._blob()
                    self._respond(200, "application/octet-stream", body)
                elif self.path == "/status":
                    _, n, dropped, t0 = tracer._buf.drain()
                    import json
                    body = json.dumps({
                        "events": n, "dropped": dropped,
                        "t0_ns": t0,
                    }).encode()
                    self._respond(200, "application/json", body)
                else:
                    self._respond(404, "text/plain", b"not found")

            def _respond(self, code: int, ct: str, body: bytes) -> None:
                self.send_response(code)
                self.send_header("Content-Type", ct)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_: Any) -> None:
                pass  # suppress access log

        srv = http.server.HTTPServer((host, port), _Handler)
        t = threading.Thread(target=srv.serve_forever, daemon=True, name="rxnet-trace")
        t.start()
        self._server = t
        return self

    # ── internal ─────────────────────────────────────────────────────────

    def _blob(self) -> bytes:
        events, n, dropped, t0 = self._buf.drain()
        return self._header(n, dropped, t0) + self._name_table() + events

    def _header(self, n: int, dropped: int, t0: int) -> bytes:
        flags = (0x01 if self._phases else 0x00) | 0x02   # bit1 = has_names (always)
        return struct.pack("<4sBBBBQII8x",
                           _MAGIC, _VERSION, 16, _LANG_PY, flags,
                           t0, n, dropped)

    def _name_table(self) -> bytes:
        """Serialize node names, FSM state names, and user event labels."""
        parts: list[bytes] = []

        # node names
        parts.append(bytes([len(self._names)]))
        for nid in range(len(self._names)):
            name = self._names[nid].encode()
            parts.append(bytes([len(name)]) + name)

        # FSM state names (one entry per machine with state_names set)
        from .fsm import Machine
        fsm_entries = [
            (nid, node) for nid, node in enumerate(self._nodes)
            if isinstance(node, Machine) and node.state_names
        ]
        parts.append(bytes([len(fsm_entries)]))
        for nid, node in fsm_entries:
            snames = node.state_names  # type: ignore[union-attr]
            assert snames is not None
            parts.append(bytes([nid, len(snames)]))
            for sid, sname in snames.items():
                enc = sname.encode()
                parts.append(bytes([sid, len(enc)]) + enc)

        # PN place / transition names
        from .pn import Net
        pn_entries = [
            (nid, node) for nid, node in enumerate(self._nodes)
            if isinstance(node, Net)
        ]
        parts.append(bytes([len(pn_entries)]))
        for nid, pn_node in pn_entries:
            pnames = pn_node.place_names or {}
            tnames = pn_node.transition_names or []
            parts.append(bytes([nid, len(pnames), len(tnames)]))
            for pid, pname in pnames.items():
                enc = pname.encode()
                parts.append(bytes([pid, len(enc)]) + enc)
            for i, tname in enumerate(tnames):
                enc = tname.encode()
                parts.append(bytes([i, len(enc)]) + enc)

        # user event labels
        parts.append(bytes([len(self._labels)]))
        for label, lid in self._labels.items():
            enc = label.encode()
            parts.append(bytes([lid, len(enc)]) + enc)

        return b"".join(parts)


# ── HTML report template ──────────────────────────────────────────────────────

def _html_diagram(name: str, kind: str, dot: str) -> str:
    # Each diagram gets a unique id so the JS can find the render target.
    safe_id = name.replace(" ", "_").replace("-", "_")
    return f"""
  <section>
    <h2>{name} <small>({kind})</small></h2>
    <div class="diagram" id="diagram_{safe_id}">
      <pre class="dot-src" style="display:none">{dot}</pre>
      <div class="render-target"></div>
    </div>
  </section>"""


_HTML_TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>{title}</title>
  <style>
    body      {{ font-family: system-ui, sans-serif; max-width: 1100px;
                margin: 0 auto; padding: 2rem; color: #222; }}
    h1        {{ border-bottom: 2px solid #ddd; padding-bottom: .5rem; }}
    h2        {{ margin-top: 2rem; color: #444; }}
    small     {{ color: #888; font-weight: normal; font-size: .75em; }}
    section   {{ margin-bottom: 2rem; }}
    .diagram  {{ background: #fafafa; border: 1px solid #e8e8e8;
                border-radius: 6px; padding: 1rem; overflow-x: auto; }}
    svg       {{ max-width: 100%; height: auto; }}
  </style>
</head>
<body>
  <h1>rxnet · {title}</h1>
  {diagrams}
  <script type="module">
    // Graphviz via WebAssembly — no installation needed.
    // Replace the CDN URL with a local copy for offline use.
    import {{ Graphviz }} from
      "https://cdn.jsdelivr.net/npm/@hpcc-js/wasm@2/dist/graphviz.umd.js";

    const gv = await Graphviz.load();
    document.querySelectorAll(".diagram").forEach(section => {{
      const src = section.querySelector(".dot-src").textContent.trim();
      const svg = gv.layout(src, "svg", "dot");
      section.querySelector(".render-target").innerHTML = svg;
    }});
  </script>
</body>
</html>
"""
