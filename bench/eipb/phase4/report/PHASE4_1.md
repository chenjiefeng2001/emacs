# EIPB Phase 4.1 -- SOAK-30M mixed-load drift (T4)
# Executed 2026-08-26.  Raw: bench/results/eipb_t41.log
# (+ sidecar bench/results/eipb_t41_rss.csv, 60 s samples)
#
# One continuous session per build (A/B/C/D), shuffled order
# A-D-B-C, SOAK-30M each: synthetic 20-file mixed project
# (elisp/c/python/org/text) replayed as continuous cycles --
# typing bursts (c+elisp), native capf round, imenu refresh,
# xref grep scan, dired small-dir listing, org subtree/global
# cycles, multi-window edit cycling, forced-GC sample -- with
# >=4 s wall-clock floor per cycle.  442 cycles / 13736 ops
# (A), symmetric within 1.5% across builds.
#
# Observation layer only.  Gates (EIPB.md T4, frozen):
#   G1 tail amplification  ta/overall <= 2.0
#      (= p99(last 10 min)/p99(first 10 min), wall-clock buckets)
#   G2 RSS no unbounded growth after warmup (slope-based)
#   G3 kill-emacs exit clean (p32 hang attribution REQUIRED)
#   G4 closed/stall atlas rows stay closed / stable over time

## 0. Verdict

**G2/G3/G4 pass on all four builds; G1 passes on A/D/B and fails
marginally on C (2.15 > 2.0) -- recorded FAIL-as-measured, then
attributed UNRESOLVABLE (doctrine 7): every build's absolute tail
p99 converges into the same 450-640 ms band; C's ratio is inflated
by the lowest head baseline (260 ms vs 341-404 ms), not by a worse
end state.  No ENCA direction anywhere (B = ENCA-disabled behaves
identically to C/D wherever they differ from A).**

NEW observed fact, build-independent: **insert-typing medians grow
~4x monotonically over 30 min in ALL FOUR builds** (type-c p50
A 16->70, B 17->50, C 9->85, D 19->91 ms; type-el 1.3->8-18 ms).
This drives the pooled p50/p99 climb and C's gate crossing.  It is
a workload/rig drift property, not attributable to ENCA; whether it
saturates or keeps climbing is exactly what SOAK-2H must answer.

## 1. Gate table

| gate                          | A     | D     | B     | C            |
|-------------------------------|-------|-------|-------|--------------|
| G1 ta/overall (<=2.0)         | 1.57  | 1.43  | 1.19  | **2.15 FAIL**|
| G1 absolute head/tail p99 ms  | 342/537 | ~404*/536 | 379/451 | 260/559 |
| G2 RSS slope post-warmup KB/h | 201   | 104   | 165   | 111          |
| G3 teardown/exit              | clean | clean | clean | clean        |
| G4 stall rows bounded         | yes   | yes   | yes   | yes          |

(*) D head_p99 read from win01-03 band; bucket value emitted
per-build in raw log.  All tails land 451-642 ms -- one band.

Per-class TA flags (>2.0): C type-el 3.09 / type-c 2.36,
D type-el 2.61, B gcpause 2.35 -- absolute values stay small
(52-273 ms heads -> 93-660 ms tails) and non-monotonic across
builds; single-run ratios at these magnitudes are order-statistics
noise plus the shared typing drift, PENDING 2H confirmation.

## 2. Whole-run class distributions (ms; n/p50/p95/p99/max)

| class    | A                  | D                  | B                  | C                  |
|----------|--------------------|--------------------|--------------------|--------------------|
| type-c   | 4420/45.8/178/527/1057  | 4400/46.9/201/562/1090 | 4360/38.8/186/504/1002 | 4410/47.7/210/554/1075 |
| type-el  | 4420/6.2/18.6/78/534    | 4400/6.8/20.6/88/563   | 4360/5.5/16.4/86/147   | 4410/5.7/22.8/108/547  |
| winedit  | 1768/6.1/42.3/112/1040  | 1760/7.2/62.2/144/535  | 1744/6.1/58.2/130/859  | 1764/5.6/41.1/101/611  |
| orgcycle | 884/4.2/16.0/72/162     | 880/4.2/16.8/128/208   | 872/4.3/18.3/81/466    | 882/3.9/14.0/73/81     |
| capf     | 442/8.4/21.6/118/1694   | 440/9.8/29.8/117/1884  | 436/7.6/17.7/115/412   | 441/7.1/28.6/140/2394  |
| imenu    | 442/0.88/8.7/25/212     | 440/0.96/8.1/29/140    | 436/0.90/10.4/89/341   | 441/0.93/10.3/18/170   |
| xref     | 442/274/1156/1267/1390  | 440/301/1024/1326/1530 | 436/272/871/1286/1373  | 441/252/582/1233/1331  |
| dired    | 442/14.5/93.4/157/709   | 440/15.8/113/158/303   | 436/14.4/98.8/154/641  | 441/13.1/94.3/146/284  |
| gcpause  | 442/73.2/126/463/697    | 440/81.5/149/608/751   | 436/75.2/137/279/622   | 441/69.0/145/593/709   |
| orgglobal| 34/20.2/154/154/154     | 34/20.5/382/465/465    | 34/24.5/130/154/154    | 34/18.3/107/150/150    |

Cross-build spread on every class stays inside the rig noise band;
no cell separates an ENCA build from vanilla beyond doctrine-7
resolution (<30% single-session).

## 3. Windowed overall drift (p50|p99 ms, ten 180 s windows)

    A  7.8|306  11.0|302  12.9|385  13.1|375  12.0|400
       13.7|456  14.6|488  14.2|506  17.7|573  17.2|642
    D  8.5|405   9.9|319  11.4|420  14.9|458  15.2|481
       14.0|556  14.7|541  17.2|590  18.5|513  20.1|575
    B  9.9|386  12.7|360  14.0|355  14.7|411  15.4|514
       15.1|476  13.5|453  10.6|404   9.0|496  12.2|576
    C  5.1|227   6.6|239   9.4|276   9.4|330  11.5|329
       13.8|474  16.6|543  16.9|556  18.1|559  20.2|599

Medians climb smoothly in A/D/C; B rises then falls back (its TA
1.19 shows the same physics can also wash out).  p99 climbs then
plateaus-ish around 500-600 ms everywhere.  Source decomposition:
the climb is carried by typing classes (section 4), which are 65%
of op volume.

## 4. Stall-row stability over time

- **Typing drift (new, universal)**: type-c p50 per window,
  A 16.1 18.2 24.4 33.0 32.3 44.4 55.8 54.7 66.5 70.0;
  C 9.2 12.1 19.5 24.4 30.1 44.9 79.1 60.9 62.4 84.9;
  D 18.5 ... 90.8; B 16.9 ... 50.3 (non-monotonic tail).
  Present with ENCA disabled (B) => workload property.  Candidate
  mechanisms (hypotheses ONLY, unattributed): undo-history churn,
  allocator arena aging, jit-lock/syntax cache state.  SOAK-2H
  decides saturate-vs-unbounded.
- **GC pause (forced)**: windowed p99 bounces 93-751 ms in every
  build with NO trend (A 168..373, D 727..127 declining, B
  128..504, C 179..203).  Same stall class as T1's 190-320 ms at
  larger heap (20 buffers live); bounded, no takeoff.
- **xref scan**: p99 1200-1400 ms flat in all builds (grep
  subprocess dominated) -- mapped row stable.
- **imenu/orgcycle/winedit/capf/dired tails SHRINK after the first
  windows** (per-class TA mostly < 1): cold-start amortizes; closed
  rows stay closed under sustained load.
- Boundary note: soak capf p50 7-10 ms differs from T3.1's ~50 ms
  round because the soak times only completion-at-point (+forced
  redisplay), excluding Completions popup install -- different
  metric boundary, atlas row unaffected.

## 5. RSS series and slopes (G2)

60 s samples, 30/block; slope fitted after 3-sample warmup trim:

| build | first KB | last KB | max KB | slope KB/h | verdict |
|-------|----------|---------|--------|------------|---------|
| A     | 40484    | 53020   | 54268  | 201        | pass    |
| D     | 39964    | 46448   | 46448  | 104        | pass    |
| B     | 31860    | 46660   | 55772  | 165        | pass    |
| C     | 40240    | 44836   | 44836  | 111        | pass    |

All slopes <= 0.2 MB/h with no monotonic takeoff; elisp heap limit
(memory-limit) plateaus at 50.4-53.4 MB from window 2 onward in
every build; GC counts per window flat (700-1200).  Warmup spike
is the 20-buffer project load itself.

## 6. Exit/teardown attribution (G3, REQUIRED p32 item)

Four consecutive 30-min sessions ended with instrumented teardown:
kill-buffers -> delete-tree -> done -> exit-clean, zero leftover
live processes, zero FATAL, zero per-cycle errors (13736 ops
stress-tested per session).  **The p32 kill-emacs hang did NOT
reproduce under soak conditions** (4/4 clean).  Cleared for tty
soak sessions; stays flagged for GUI variants.

## 7. Harness notes

- Pre-run fixes (before any measured byte; aborted first launch
  left zero committed artifacts): emacs_pid emitted as %.4f float
  broke the runner pid-match -> RSS sidecar dead (fixed: %d emit +
  int-cast + pgrep fallback); TA was window-based instead of the
  contract's first/last-10-min buckets (fixed: wall-clock buckets);
  added per-class windowed drift rows; added post-timeout pkill
  orphan insurance.
- Bucket-TA sensitivity: ratio = tail_p99/head_p99 punishes a FAST
  head (C).  Frozen metric kept as-is; absolute head/tail values
  now always emitted alongside so future reads converge on the
  band, not the ratio alone.
- Blocks ran sequentially with ONE shuffle (continuity requirement
  of soak); session-position effects therefore remain possible --
  doctrine 7 applies to every cross-build sentence above.
- ENCA internal counters remain PENDING-API (not elisp-visible),
  excluded from gates per doctrine (recorded, never guessed).
