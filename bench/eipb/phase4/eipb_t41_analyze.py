#!/usr/bin/env python3
# EIPB Phase 4.1 SOAK analyzer.
# Parses bench/results/eipb_t41.log (+ eipb_t41_rss.csv sidecar) into
# the tables PHASE4.md needs: run integrity, whole-run class
# distributions, tail-amplification gates, windowed drift matrices,
# GC/memory-limit drift, RSS slopes.
import re
import sys
from collections import defaultdict

# usage: eipb_t41_analyze.py [log] [rss]   (defaults = T4.1 artifacts)
LOG = sys.argv[1] if len(sys.argv) > 1 else \
    "/mnt/c/Users/14977/source/repos/emacs/bench/results/eipb_t41.log"
RSS = sys.argv[2] if len(sys.argv) > 2 else \
    "/mnt/c/Users/14977/source/repos/emacs/bench/results/eipb_t41_rss.csv"

TA_GATE = 2.0          # p99(t_end) <= 2 x p99(t_0)  (frozen T4 gate)
WARMUP_SAMPLES = 3     # RSS samples trimmed before slope fit

row = re.compile(r"^EIPB4\|([^|]+)\|([^|]+)\|([^|]+)\|(.*)$")

meta = defaultdict(dict)              # build -> key -> val
win = defaultdict(dict)               # build -> (win,metric) -> v
wincls = defaultdict(dict)            # build -> (win,class,metric) -> v
cls = defaultdict(lambda: defaultdict(dict))   # build -> class -> metric -> v
ta = defaultdict(lambda: defaultdict(dict))    # build -> cell -> metric -> v
notes = defaultdict(list)             # build -> [(cell,text)]
status = {}                           # build -> clean|HANG...
builds = []                           # first-seen order
order = []
run_begin = run_done = None

for line in open(LOG, encoding="utf-8", errors="replace"):
    line = line.strip()
    if line.startswith("EIPB4_RUN_BEGIN|"):
        run_begin = int(line.split("|")[1]); continue
    if line.startswith("EIPB4_RUN_DONE|"):
        run_done = int(line.split("|")[1]); continue
    if line.startswith("EIPB4_ORDER|"):
        order.append(line.split("|")[1]); continue
    m = row.match(line)
    if not m:
        continue
    b, cell, metric, val = m.groups()
    if cell == "meta":
        meta[b][metric] = val
    elif cell == "block":
        if metric == "begin":
            status.setdefault(b, "?")
            if b not in builds:
                builds.append(b)
    elif cell == "teardown" and metric == "status":
        status[b] = val
    elif cell.startswith("win") and "/" in cell:
        w, c = cell.split("/", 1)
        try:
            wincls[b][(w, c, metric)] = float(val)
        except ValueError:
            pass
    elif cell.startswith("win"):
        try:
            win[b][(cell, metric)] = float(val)
        except ValueError:
            pass
    elif cell.startswith("cls/"):
        try:
            cls[b][cell[4:]][metric] = float(val)
        except ValueError:
            pass
    elif cell.startswith("ta/"):
        try:
            ta[b][cell[3:]][metric] = float(val)
        except ValueError:
            pass
    else:
        notes[b].append((cell, metric, val))

builds = list(status.keys())
print("== RUN ==")
print("order:", "-".join(builds))
if run_begin and run_done:
    print("wall hours: %.2f" % ((run_done - run_begin) / 3600.0))
for b in builds:
    m = meta[b]
    print("%s: status=%s wall_min=%.1f ops=%d cyc=%d wins=%s pid=%s" % (
        b, status.get(b, "?"),
        float(m.get("wall_ms", 0)) / 60000.0,
        int(float(m.get("ops", 0))), int(float(m.get("cycles", 0))),
        m.get("windows_completed", "?"), m.get("emacs_pid", "?")))
err_notes = [(b, c, v) for b in builds for (c, mm, v) in notes[b]
             if "err" in v.lower()]
for b, c, v in err_notes[:20]:
    print("note %s %s %s" % (b, c, v))

print("\n== WHOLE-RUN CLASS DISTRIBUTIONS (ms; n/p50/p95/p99/max) ==")
classes = sorted({c for b in builds for c in cls[b]})
hdr = "%-12s" + "%22s" * len(builds)
print(hdr % tuple(["class"] + ["%s" % b for b in builds]))
for c in classes:
    row_cells = []
    for b in builds:
        d = cls[b].get(c)
        if not d:
            row_cells.append("-")
        else:
            row_cells.append("%.0f/%.2f/%.1f/%.1f/%.0f" % (
                d.get("n", 0), d.get("p50", 0), d.get("p95", 0),
                d.get("p99", 0), d.get("max", 0)))
    print("%-12s" % c + "%22s" * len(builds) % tuple(row_cells))

print("\n== TAIL AMPLIFICATION (p99 last-10min / p99 first-10min) ==")
print("gate: overall ratio <= %.1f ; per-class flagged at >%.1f" % (TA_GATE, TA_GATE))
tas = ["overall"] + classes
for b in builds:
    parts = []
    for c in tas:
        d = ta[b].get(c)
        if d and "ta_ratio" in d:
            parts.append("%s=%.2f(n=%d/%d)" % (
                c, d["ta_ratio"], int(d.get("head_n", 0)),
                int(d.get("tail_n", 0))))
    print(b, "; ".join(parts))

print("\n== WINDOWED OVERALL DRIFT (p50 | p99 ms, per 180s window) ==")
for b in builds:
    ws = sorted({w for (w, _m) in win[b]}, key=lambda x: int(x[3:]))
    cells = []
    for w in ws:
        cells.append("%.1f|%.0f" % (win[b][(w, "p50")], win[b][(w, "p99")]))
    print(b, "  ".join(cells))
print("(columns are windows in run order)")

print("\n== WINDOWED CLASS DRIFT p50 (ms) -- stall stability check ==")
stall_classes = [c for c in classes]
for b in builds:
    ws = sorted({w for (w, _c, _m) in wincls[b]}, key=lambda x: int(x[3:]))
    for c in ("gcpause",):
        vals = ["%.0f" % wincls[b][(w, c, "p99")] for w in ws if (w, c, "p99") in wincls[b]]
        if vals:
            print(b, c, "p99:", " ".join(vals))

# ------------- typing drift curve (T4.2 core question) -------------

def lin_slope(ys, dx=1.0):
    n = len(ys)
    if n < 2:
        return 0.0
    xs = [i * dx for i in range(n)]
    mx, my = sum(xs) / n, sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs)
    return num / den if den else 0.0

print("\n== TYPING DRIFT CURVE (p50 ms per 180s window) ==")
print("saturating iff last-30min slope collapses vs first-30min")
for b in builds:
    ws = sorted({w for (w, _c, _m) in wincls[b]}, key=lambda x: int(x[3:]))
    for c in ("type-c", "type-el"):
        ser = [wincls[b][(w, c, "p50")] for w in ws
               if (w, c, "p50") in wincls[b]]
        if len(ser) < 10:
            continue
        nw = len(ser)
        k = max(1, round(nw / 3))          # ~30 min of windows
        head, tail = ser[:k], ser[-k:]
        sh = lin_slope(head, 3.0)          # ms per minute
        st = lin_slope(tail, 3.0)
        plateau = sum(ser[-5:]) / 5.0
        verdict = ("SATURATING" if abs(st) < 0.5 * abs(sh) or abs(st) < 0.05
                   else "NON-SATURATING-WITHIN-RUN")
        print("%s %s: w1=%.1f plateau(last5)=%.1f "
              "slope_first%dwin=%+.3f slope_last%dwin=%+.3f ms/min -> %s"
              % (b, c, ser[0], plateau, k, sh, k, st, verdict))
        print("   series:", " ".join("%.1f" % v for v in ser))

print("\n== WINDOWED gcs_delta + memlimit_kb ==")
for b in builds:
    ws = sorted({w for (w, _m) in win[b]}, key=lambda x: int(x[3:]))
    g = [int(win[b][(w, "gcs_delta")]) for w in ws]
    mm = [int(win[b][(w, "memlimit_kb")]) for w in ws]
    print(b, "gcs:", sum(g), g)
    print(b, "memlimit:", mm)

# ---------------- RSS ----------------
rss = defaultdict(list)
try:
    for line in open(RSS):
        parts = line.strip().split(",")
        if len(parts) == 3:
            rss[parts[0]].append((int(parts[1]), int(parts[2])))
except FileNotFoundError:
    print("\nRSS MISSING")

def slope_kbh(samples):
    xs = [t / 60.0 for t, _ in samples]
    ys = [v for _, v in samples]
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs)
    return num / den if den else 0.0

print("\n== RSS (KB; slope over post-warmup samples) ==")
for b in builds:
    s = sorted(rss.get(b, []))
    if not s:
        print(b, "no samples"); continue
    trimmed = s[WARMUP_SAMPLES:] or s
    sl = slope_kbh(trimmed)
    vals = [v for _, v in s]
    print("%s n=%d first=%d last=%d min=%d max=%d "
          "slope=%.0f KB/h(max-median=%d)" % (
              b, len(s), vals[0], vals[-1], min(vals), max(vals),
              sl, max(vals) - sorted(vals)[len(vals) // 2]))
