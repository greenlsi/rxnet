# Copyright 2026 Jose M. Moya <jm.moya@upm.es>
# SPDX-License-Identifier: GPL-3.0-or-later

"""rxnet-trace — download and visualize rxnet traces.

Usage::

    python -m rxnet.tools.trace http://target:7777 --report report.html --open
    python -m rxnet.tools.trace trace.bin          --report report.html --open
    python -m rxnet.tools.trace http://target:7777 --stats
"""
from __future__ import annotations

import argparse
import struct
import urllib.request
import webbrowser
from pathlib import Path

_MAGIC   = b"RXNT"
_EV_SIZE = 16
_PACK    = struct.Struct("<QBBHHH")

# event kind constants (must match trace.py)
_EV_TICK, _EV_N_START, _EV_N_END = 0, 1, 2
_EV_PH_START, _EV_PH_END         = 3, 4
_EV_FSM, _EV_PN, _EV_USER        = 5, 6, 7

_PHASE_NAMES = ["latch", "evaluate", "commit", "dump"]


# ── binary parser ─────────────────────────────────────────────────────────────

class _Trace:
    """Parsed binary blob."""

    def __init__(self, blob: bytes) -> None:
        if blob[:4] != _MAGIC:
            raise ValueError("Not an rxnet trace file (bad magic)")

        # header (32 bytes fixed)
        hdr = struct.unpack_from("<4sBBBBQII8x", blob, 0)
        _, version, ev_sz, lang, flags, t0, n_events, n_dropped = hdr
        self.t0_ns   = t0
        self.n_ev    = n_events
        self.dropped = n_dropped
        self.phases  = bool(flags & 0x01)
        self.lang    = chr(lang)
        off = 32

        # name table
        self.node_names: list[str]             = []
        self.fsm_states: dict[int, dict[int, str]] = {}   # nid → {sid: name}
        self.pn_places:  dict[int, dict[int, str]] = {}   # nid → {pid: name}
        self.pn_trans:   dict[int, list[str]]      = {}   # nid → [name, ...]
        self.labels:     dict[int, str]            = {}   # lid → name

        if flags & 0x02:  # has_names
            off = self._parse_names(blob, off)

        # event array
        self.events: list[tuple] = []
        for i in range(n_events):
            self.events.append(_PACK.unpack_from(blob, off + i * _EV_SIZE))

    def _parse_names(self, blob: bytes, off: int) -> int:
        # node names
        nc = blob[off]
        off += 1
        for _ in range(nc):
            nlen = blob[off]
            off += 1
            self.node_names.append(blob[off:off + nlen].decode())
            off += nlen

        # FSM state names
        fc = blob[off]
        off += 1
        for _ in range(fc):
            nid, sc = blob[off], blob[off + 1]
            off += 2
            states: dict[int, str] = {}
            for _ in range(sc):
                sid, slen = blob[off], blob[off + 1]
                off += 2
                states[sid] = blob[off:off + slen].decode()
                off += slen
            self.fsm_states[nid] = states

        # PN names
        pc = blob[off]
        off += 1
        for _ in range(pc):
            nid, np_, nt = blob[off], blob[off + 1], blob[off + 2]
            off += 3
            places: dict[int, str] = {}
            for _ in range(np_):
                pid, plen = blob[off], blob[off + 1]
                off += 2
                places[pid] = blob[off:off + plen].decode()
                off += plen
            self.pn_places[nid] = places
            trans: list[str] = []
            for _ in range(nt):
                _tid, tlen = blob[off], blob[off + 1]
                off += 2
                trans.append(blob[off:off + tlen].decode())
                off += tlen
            self.pn_trans[nid] = trans

        # user labels
        lc = blob[off]
        off += 1
        for _ in range(lc):
            lid, llen = blob[off], blob[off + 1]
            off += 2
            self.labels[lid] = blob[off:off + llen].decode()
            off += llen
        return off

    def node_name(self, nid: int) -> str:
        if nid < len(self.node_names):
            return self.node_names[nid]
        return f"node_{nid}"

    def state_name(self, nid: int, sid: int) -> str:
        return self.fsm_states.get(nid, {}).get(sid, f"s{sid}")

    def label_name(self, lid: int) -> str:
        return self.labels.get(lid, f"label_{lid}")


# ── stats ─────────────────────────────────────────────────────────────────────

def _stats(trace: _Trace) -> None:
    print(f"events: {trace.n_ev}  dropped: {trace.dropped}  phases: {trace.phases}")
    print(f"t0_ns:  {trace.t0_ns}  lang: {trace.lang}")

    # WCRT per node
    starts: dict[int, int] = {}
    wcrt:   dict[int, int] = {}
    for t_ns, kind, nid, a, b, _c in trace.events:
        if kind == _EV_N_START:
            starts[nid] = t_ns
        elif kind == _EV_N_END and nid in starts:
            rt = t_ns - starts.pop(nid)
            wcrt[nid] = max(wcrt.get(nid, 0), rt)

    if wcrt:
        print("\nWorst-case response time per node:")
        for nid, w in sorted(wcrt.items()):
            print(f"  {trace.node_name(nid):20s}  {w / 1000:.1f} µs")


# ── Perfetto JSON ─────────────────────────────────────────────────────────────

def _to_perfetto(trace: _Trace) -> dict:
    events: list[dict] = []
    # pid=0 = this runtime; tid = node index; tid=9999 = user events

    starts: dict[int, int]        = {}
    ph_starts: dict[tuple, int]   = {}  # (nid, phase) → t_ns
    state_vals: dict[int, int]    = {}

    for t_ns, kind, nid, a, b, _c in trace.events:
        t_us = t_ns / 1_000   # Perfetto uses microseconds
        name = trace.node_name(nid)

        if kind == _EV_TICK:
            events.append({"ph": "i", "name": f"slot {a}", "cat": "tick",
                           "ts": t_us, "pid": 0, "tid": 9998, "s": "g"})

        elif kind == _EV_N_START:
            starts[nid] = t_ns

        elif kind == _EV_N_END:
            if nid in starts:
                dur_us = (t_ns - starts.pop(nid)) / 1_000
                events.append({"ph": "X", "name": name, "cat": "node",
                               "ts": t_us - dur_us, "dur": dur_us,
                               "pid": 0, "tid": nid})

        elif kind == _EV_PH_START:
            ph_starts[(nid, a)] = t_ns

        elif kind == _EV_PH_END:
            key = (nid, a)
            if key in ph_starts:
                dur_us = (t_ns - ph_starts.pop(key)) / 1_000
                events.append({"ph": "X", "name": _PHASE_NAMES[a], "cat": "phase",
                               "ts": t_us - dur_us, "dur": dur_us,
                               "pid": 0, "tid": nid})

        elif kind == _EV_FSM:
            src = trace.state_name(nid, a)
            dst = trace.state_name(nid, b)
            events.append({"ph": "i", "name": f"{src} → {dst}", "cat": "fsm",
                           "ts": t_us, "pid": 0, "tid": nid, "s": "t"})
            if state_vals.get(nid) != b:
                state_vals[nid] = b
                events.append({"ph": "C", "name": "state",
                               "ts": t_us, "pid": 0, "tid": nid,
                               "args": {"value": b}})

        elif kind == _EV_PN:
            tname = (trace.pn_trans.get(nid) or [])
            label = tname[a] if a < len(tname) else f"T{a}"
            events.append({"ph": "i", "name": f"{label} fired", "cat": "pn",
                           "ts": t_us, "pid": 0, "tid": nid, "s": "t"})

        elif kind == _EV_USER:
            events.append({"ph": "i", "name": f"{trace.label_name(a)}={b}",
                           "cat": "user", "ts": t_us,
                           "pid": 0, "tid": 9999, "s": "g"})

    # thread metadata
    for nid in range(len(trace.node_names)):
        events.append({"ph": "M", "name": "thread_name",
                       "pid": 0, "tid": nid, "args": {"name": trace.node_name(nid)}})

    return {"traceEvents": events}


# ── entry point ──────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description="rxnet trace decoder")
    ap.add_argument("source",
                    help="http://host:port  or  path/to/trace.bin")
    ap.add_argument("--report", metavar="PATH",
                    help="write standalone HTML report")
    ap.add_argument("--perfetto", metavar="PATH",
                    help="write Perfetto JSON")
    ap.add_argument("--stats", action="store_true",
                    help="print WCRT summary to stdout")
    ap.add_argument("--open", action="store_true",
                    help="open HTML report in browser after writing")
    args = ap.parse_args()

    # fetch
    src = args.source
    if src.startswith("http://") or src.startswith("https://"):
        url = src if src.endswith("/trace") else src.rstrip("/") + "/trace"
        with urllib.request.urlopen(url) as r:
            blob = r.read()
    else:
        blob = Path(src).read_bytes()

    trace = _Trace(blob)

    if args.stats:
        _stats(trace)

    if args.perfetto:
        import json
        Path(args.perfetto).write_text(json.dumps(_to_perfetto(trace)), encoding="utf-8")
        print(f"Perfetto JSON → {args.perfetto}")

    if args.report:
        _write_report(trace, args.report)
        print(f"HTML report   → {args.report}")
        if args.open:
            webbrowser.open(Path(args.report).resolve().as_uri())


def _write_report(trace: _Trace, path: str) -> None:
    """Generate standalone HTML report from a parsed trace."""
    # WCRT table
    starts: dict[int, int] = {}
    wcrt:   dict[int, int] = {}
    for t_ns, kind, nid, _a, _b, _c in trace.events:
        if kind == _EV_N_START:
            starts[nid] = t_ns
        elif kind == _EV_N_END and nid in starts:
            rt = t_ns - starts.pop(nid)
            wcrt[nid] = max(wcrt.get(nid, 0), rt)

    rows = "\n".join(
        f"<tr><td>{trace.node_name(nid)}</td>"
        f"<td>{w / 1000:.1f} µs</td></tr>"
        for nid, w in sorted(wcrt.items())
    )

    # Perfetto JSON for embedding
    import json
    perf_json = json.dumps(_to_perfetto(trace))

    html = _REPORT_TEMPLATE.format(
        title    = "rxnet trace",
        wcrt_rows= rows,
        events   = trace.n_ev,
        dropped  = trace.dropped,
        perf_json= perf_json,
    )
    Path(path).write_text(html, encoding="utf-8")


_REPORT_TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>{title}</title>
  <style>
    body  {{ font-family: system-ui, sans-serif; max-width: 1100px;
            margin: 0 auto; padding: 2rem; color: #222; }}
    h1    {{ border-bottom: 2px solid #ddd; padding-bottom: .5rem; }}
    h2    {{ margin-top: 2rem; color: #444; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th,td {{ border: 1px solid #ddd; padding: .4rem .8rem; text-align: left; }}
    th    {{ background: #f5f5f5; }}
    .meta {{ color: #888; font-size: .85em; }}
    button {{ padding: .5rem 1.2rem; background: #1a73e8; color: #fff;
              border: none; border-radius: 4px; cursor: pointer; font-size: 1em; }}
    button:hover {{ background: #1557b0; }}
  </style>
</head>
<body>
  <h1>rxnet · {title}</h1>
  <p class="meta">events: {events} &nbsp;|&nbsp; dropped: {dropped}</p>

  <h2>Worst-case response time</h2>
  <table>
    <tr><th>Node</th><th>WCRT</th></tr>
    {wcrt_rows}
  </table>

  <h2>Timeline</h2>
  <button id="open-perfetto">Open in Perfetto ↗</button>

  <script>
    const PERFETTO_URL = "https://ui.perfetto.dev";
    const perf = {perf_json};

    document.getElementById("open-perfetto").addEventListener("click", () => {{
      // Encode the Chrome Trace JSON as UTF-8 bytes.
      const bytes = new TextEncoder().encode(JSON.stringify(perf));

      // Perfetto postMessage protocol:
      //   1. Open ui.perfetto.dev and post "PING" until it responds "PONG".
      //   2. On "PONG", send {{ perfetto: {{ buffer, title }} }}.
      const win = window.open(PERFETTO_URL);
      let interval;

      function onMessage(evt) {{
        if (evt.source !== win || evt.data !== "PONG") return;
        clearInterval(interval);
        window.removeEventListener("message", onMessage);
        win.postMessage(
          {{ perfetto: {{ buffer: bytes, title: "rxnet trace", fileName: "rxnet.json" }} }},
          PERFETTO_URL
        );
      }}

      window.addEventListener("message", onMessage);
      // Poll with PING until Perfetto is ready (it ignores PINGs before init).
      interval = setInterval(() => win.postMessage("PING", PERFETTO_URL), 200);
    }});
  </script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
