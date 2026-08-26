#!/usr/bin/env python3
# Rider R1 (PHASE5.md section 4, rung 1) -- PURE RE-READ.
# No new runs, no instrumentation, no harness changes: parses the
# already-committed/emitted eipb_t41/t42 logs (+ rss sidecars) and
# asks ONE narrow question:
#   is the B/C latency drift synchronized with GC / memory-pressure
#   state changes?
# Known missing fields (recorded, never guessed):
#   - gc-cons-threshold was never emitted -> MISSING-FIELD
#   - windowed type-c has p50/p99/max only -> no windowed p95
import re, math
from collections import defaultdict

import os
BASE = "/mnt/c/Users/14977/source/repos/emacs/bench/results"
if not os.path.isdir(BASE):
    BASE = "C:/Users/14977/source/repos/emacs/bench/results"
RUNS = [("t41", BASE + "/eipb_t41.log", BASE + "/eipb_t41_rss.csv"),
        ("t42", BASE + "/eipb_t42.log", BASE + "/eipb_t42_rss.csv")]

row = re.compile(r"^EIPB4\|([^|]+)\|([^|]+)\|([^|]+)\|(.*)$")
WIN = 180.0


def parse(log):
    wins = defaultdict(dict)   # build -> win -> {metric: val}
    wcls = defaultdict(dict)   # build -> (win, cls) -> {metric: val}
    begin = {}
    order = []
    expect_order = False
    for line in open(log, encoding="utf-8", errors="replace"):
        line = line.strip()
        m = row.match(line)
        if not m:
            if line.startswith("EIPB4_ORDER|"):
                order.append(line.split("|")[1])
                expect_order = True
            elif expect_order and line in ("A", "B", "C", "D"):
                order.append(line)
            else:
                expect_order = False
            continue
        expect_order = False
        b, cell, met, val = m.groups()
        try:
            v = float(val)
        except ValueError:
            continue
        if cell == "block" and met == "begin":
            begin[b] = int(val)
        elif cell.startswith("win") and "/" in cell:
            w, c = cell.split("/", 1)
            wcls[b].setdefault((w, c), {})[met] = v
        elif cell.startswith("win"):
            wins[b].setdefault(cell, {})[met] = v
    return order, wins, wcls, begin


def parse_rss(path):
    ser = defaultdict(list)
    for line in open(path):
        p = line.strip().split(",")
        if len(p) == 3 and p[0] in ("A", "B", "C", "D"):
            ser[p[0]].append((int(p[1]), int(p[2])))
    return ser


def spearman(x, y):
    n = len(x)

    def rank(v):
        idx = sorted(range(n), key=lambda i: v[i])
        r = [0.0] * n
        i = 0
        while i < n:
            j = i
            while j + 1 < n and v[idx[j + 1]] == v[idx[i]]:
                j += 1
            avg = (i + j) / 2 + 1
            for k in range(i, j + 1):
                r[idx[k]] = avg
            i = j + 1
        return r

    rx, ry = rank(x), rank(y)
    mx, my = sum(rx) / n, sum(ry) / n
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    den = math.sqrt(sum((a - mx) ** 2 for a in rx) *
                    sum((b - my) ** 2 for b in ry))
    return num / den if den else float("nan")


def med(v):
    s = sorted(v)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def analyze(run, log, rssf):
    order, wins, wcls, begin = parse(log)
    rss = parse_rss(rssf)
    print("=" * 78)
    print(f"RIDER R1 -- run {run}  order={'-'.join(order)}")
    print("=" * 78)
    hdr = ("build | w | age_min | tc_p50 | tc_p99 | ops | gcs | "
           "gcs/op x1000 | mem_MB | rss_MB")
    print(hdr)
    for b in order:
        ws = sorted(wins[b].keys(), key=lambda s: int(s[3:]))
        rts = rss.get(b, [])
        b0 = begin.get(b)
        series = []
        for w in ws:
            wm = wins[b][w]
            cm = wcls[b].get((w, "type-c"), {})
            ts = b0 + int(w[3:]) * WIN if b0 else None
            r = None
            if ts and rts:
                near = min(rts, key=lambda p: abs(p[0] - ts))
                if abs(near[0] - ts) <= 120:
                    r = near[1] / 1024.0
            gop = (wm.get("gcs_delta", 0) / wm["n"] * 1000.0
                   if wm.get("n") else float("nan"))
            series.append(dict(
                w=int(w[3:]),
                age=(int(w[3:]) * WIN) / 60.0,
                p50=cm.get("p50"), p99=cm.get("p99"),
                n=wm.get("n"), gcs=wm.get("gcs_delta"),
                gop=gop, mem=(wm.get("memlimit_kb", 0) / 1024.0),
                rss=r))
        for s in series:
            rs = f"{s['rss']:6.1f}" if s["rss"] else "   n/a"
            print(f"  {b} | {s['w']:2d} | {s['age']:6.1f} | "
                  f"{s['p50']:7.1f} | {s['p99']:7.1f} | "
                  f"{s['n']:5.0f} | {s['gcs']:5.0f} | "
                  f"{s['gop']:8.2f} | {s['mem']:6.1f} | {rs}")
        # --- synchronization stats (observation layer only) ---
        ok = [s for s in series if s["p50"] is not None]
        L = [s["p50"] for s in ok]
        G = [s["gop"] for s in ok]
        M = [s["mem"] for s in ok]
        N = [s["n"] for s in ok]
        h = len(L) // 2
        print(f"  -- {b} sync stats --")
        print(f"  sp(L_p50, mem_MB)      = {spearman(L, M):+.3f}")
        print(f"  sp(L_p50, gcs/op x1000)= {spearman(L, G):+.3f}")
        print(f"  sp(L_p50, ops/win)     = {spearman(L, N):+.3f}")
        dL = [L[i + 1] - L[i] for i in range(len(L) - 1)]
        dM = [M[i + 1] - M[i] for i in range(len(M) - 1)]
        print(f"  sp(dL, dMem)           = {spearman(dL, dM):+.3f}")
        print(f"  gcs/op first-half mean = "
              f"{sum(G[:h]) / h:7.2f}  last-half = {sum(G[h:]) / (len(G) - h):7.2f}"
              f"  ratio={ (sum(G[h:]) / (len(G) - h)) / (sum(G[:h]) / h):5.2f}")
        base = med([x for x in L[:10]]) or 1.0
        acc = next((s["w"] for s in ok[5:]
                    if sum(xx["p50"] for xx in ok[max(0, s["w"] - 3):s["w"] + 2]) /
                    len(ok[max(0, s["w"] - 3):s["w"] + 2]) > 2.0 * base),
                   None)
        if acc:
            lo = max(0, acc - 4)
            hi = min(len(ok) - 1, acc + 3)
            gm = min(s["gop"] for s in ok[lo:hi + 1])
            mm = max(s["mem"] for s in ok[lo:hi + 1])
            print(f"  accel onset ~win{acc}: local min(gcs/op)={gm:.2f}, "
                  f"local max(mem)={mm:.1f}MB (windowed +-3)")
        print()


for run, log, rssf in RUNS:
    analyze(run, log, rssf)
print("MISSING-FIELD LEDGER:")
print("  gc-cons-threshold      : never emitted in any EIPB4 log")
print("  windowed type-c p95    : not emitted (p50/p99/max only)")
print("  workload counters      : partial (ops/win only)")
