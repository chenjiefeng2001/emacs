# EIPB Phase 4.2 -- SOAK-2H mixed-load drift (T4)
# Executed 2026-08-26.  Raw: bench/results/eipb_t42.log
# (+ sidecar bench/results/eipb_t42_rss.csv, 60 s samples)
#
# Identical harness/workload/sampling/TA-buckets as Phase 4.1
# (zero harness changes, zero runtime changes).  One continuous
# 2-hour session per build (A/B/C/D), shuffled order [ORDER],
# SECS=7200, ten-minute TA buckets per frozen contract.
#
# Purpose (set by T4.1): the build-independent typing-median drift
# (~4x over 30 min in ALL builds incl ENCA-disabled B) showed NO
# saturation within its observation window.  This run decides:
#   saturating curve  -> warm-up/steady-state property
#   unbounded growth  -> long-session degradation candidate
# plus the four frozen readings: type-c p50 shape, tail sync,
# A/B/C/D co-trajectory, RSS/GC account correlation.

## 0. Verdict

**The T4.1 typing-median drift does NOT saturate within 2 hours.**
type-c p50 ends at B ~344 / C ~295 / D ~70 / A ~65 ms with late-half
slope ACCELERATION in B (+6.9 ms/min) and C (+5.4 ms/min); only D
reaches a quasi-steady state (~65-75 ms plateau, slope flat both
halves).  The warm-up/steady-state explanation is REJECTED; the
build-independent long-session degradation candidate stands at the
>=2 h scale ("unbounded" remains unproven beyond the window).
Disabled-B posts the worst end state => no ENCA direction anywhere;
ARCHITECTURE freeze unchanged.

Gates: G2 pass x4 (RSS slope 71-96 KB/h, no takeoff); G3 pass x4
(p32 exit hang now 8/8 clean soak exits incl. T4.1); G4 stall rows
bounded except the typing row itself (the open row under test).
G1-style ta/overall: D 1.26 PASS; C 3.51 / A 3.75 / B 5.88 recorded
FAIL-as-measured -- and unlike T4.1 the absolute tails do NOT
converge into one band (head/tail p99: C 338->1189, A 109->409,
B 105->619, D 395->498 ms), so T4.1's UNRESOLVABLE-by-band argument
is unavailable here; single session per build with session position
fully confounded => UNRESOLVED, handed to doctrine-3 multi-session.

## 1. Typing drift curve -- saturation question

| build | type-c w1 | plateau(last5) | slope first13 | slope last13 | reading |
|-------|----------:|---------------:|--------------:|-------------:|---------|
| C | 16.2 | 295.4 | +2.859 | +5.421 | non-saturating, accelerating |
| D | 16.0 | 69.6  | +0.391 | +0.352 | quasi-steady (analyzer label NON-SATURATING-WITHIN-RUN; slopes small AND flat) |
| A | 3.9  | 64.9  | +0.648 | +0.557 | non-saturating, near-linear creep |
| B | 3.8  | 344.0 | +1.021 | +6.912 | non-saturating, accelerating |

type-el same shape: C 1.4->59.7 (+0.582->+1.111), B 0.5->67.4
(+0.174->+1.340), A 0.5->11.7 creep; D SATURATING (plateau 12.7).

Answer to the phase question: saturation within <=2 h REJECTED as
the general case (3/4 builds still climbing, two accelerating);
D shows a steady state IS reachable (~70 ms).  Whether D's steadiness
is a build property or a session-slot property cannot be separated
in this design (sections 3/6).

Observed transition, C only: around windows 31-32 (~93 min) its
per-window GC count spikes ~10x (552 -> 1750/1774) then collapses
(<200); the overall median dips briefly (57 -> 12.5 ms) and the
typing curve re-enters acceleration into a 190-300+ ms regime.
Recorded as observation; no attribution.

Head asymmetry (position-confounded): win01 overall p50 C/D ~8 ms
vs A/B ~2.4 ms; type-c w1 16 vs 3.8-3.9 ms.  Order was C-D-A-B, so
build identity and slot identity cannot be separated; recorded
unattributed.

## 2. Tail amplification over 2h (first/last 10-min buckets)

| build | ta/overall | head_p99 | tail_p99 | flags >2.0 (abs head->tail ms where pulled) |
|-------|-----------:|---------:|---------:|----------------------------------------------|
| C | **3.51 FAIL** | 338 | 1189 | capf 2.11 (143->302), dired 4.51 (154->695), type-c 3.68, type-el 7.04; xref flat 0.98 (1210->1189) |
| D | 1.26 pass     | 395 | 498  | type-el 2.49 only |
| A | **3.75 FAIL** | 109 | 409  | dired 3.25 (35->115), type-c 2.72, type-el 6.39; capf exactly 2.00 = not flagged |
| B | **5.88 FAIL** | 105 | 619  | imenu 13.48 (0.8->11.3, sub-noise magnitudes), xref 3.71 (141->522), capf 3.32 (28->92), dired 4.11 (51->209), type-el 5.82, type-c 2.62 |

Reading: T4.1's single converged 451-642 ms tail band does NOT hold
at 2 h -- C's tail leaves it (1189 ms), D/A/B land 409-619 ms.  The
band argument that made T4.1-C UNRESOLVABLE is therefore not
available; FAIL-as-measured stands for C/A/B, overall UNRESOLVED
(single session, position confounded, doctrine 7; resolution
requires doctrine-3 multi-session work).  Small-magnitude ratios
(B imenu 13.48 on <12 ms metrics) remain order-statistics noise per
the T4.1 precedent.  xref note: first-bucket p99 spans 140 (B) to
1212 (D) ms across builds/slots vs T4.1's flat ~1200-1400 --
grep-subprocess spawn cost tracks rig state, not editor state; the
mapped atlas row is unaffected.

## 3. Co-trajectory check (A vs B/C/D)

- Direction universal: all four windowed overall medians rise over
  2 h; end-of-run medians C ~70-81, B ~70-96, D ~12-15, A ~12-20 ms.
  With typing at 65% of op volume this is section 1's single
  phenomenon seen through the pooled lens.
- Disabled-B tracks or exceeds every enabled build's late
  acceleration and posts the worst typing end state => ENCA is
  exonerated for the drift; full-ENCA D is if anything the
  best-behaved trajectory (only quasi-steady finish).
- GC-rate co-observation (hypothesis ONLY, doctrine 8 unmet): the
  two builds whose per-window GC counts collapse late (C 959->56,
  B 3402->80) are exactly the two whose typing accelerates; A
  sustains ~1500-3200/window (mild linear creep) and D sustains
  ~700-1900/window (quasi-steady).  Candidate mechanism: heap-growth
  threshold pacing interaction -- untested, unattributed.

## 4. RSS / GC accounts vs latency correlation

| build | RSS first KB | last KB | max KB | slope KB/h |
|-------|-------------:|--------:|-------:|-----------:|
| C | 38276 | 50764 | 83576 | 96 |
| D | 39644 | 52336 | 82192 | 85 |
| A | 40924 | 50584 | 74500 | 71 |
| B | 40872 | 65444 | 79940 | 84 |

No monotonic takeoff in any build; memory-limit ratchets stepwise
+23-24 MB over 2 h in ALL FOUR builds (33-35k -> 57-59k KB).  RSS
does NOT explain the latency drift: B's slope (84) ~= A's (71)
while B's late typing slope is ~12x A's, and D ratchets identically
yet finishes quasi-steady -- latency drift and memory pressure are
decoupled at the observation layer.  Forced-GC pause stays bounded
in every build (windowed p99 bounce 43-824 ms, no trend): the T1
stall row is stable at 2 h scale.

## 5. Exit/teardown + integrity

Order C-D-A-B, one shuffle, sequential blocks (soak continuity),
wall 8.03 h total (2026-08-26 11:58 -> 20:00).  Per build:
wall_min 120.0-120.1, 40x180 s windows complete, ops
52710/54886/55384/53425 and cycles 1696/1766/1782/1719 (C/D/A/B,
symmetric within ~5%).  Teardown chain kill-buffers ->
delete-tree -> done -> exit-clean x4, teardown|status|clean x4,
EIPB4_RUN_DONE emitted; zero FATAL/error/leftover strings in the
raw log.  **p32 exit hang: not reproduced -- now 8/8 clean soak
exits including T4.1**; tty domain stays cleared, GUI variants
remain flagged.

## 6. Reading discipline

- Observation layer only; no causal language without doctrine-8
 要件 (same workload + controlled machine + randomized order +
 N sessions + effect above noise band).
- T4.1 C=2.15 stays UNRESOLVABLE; this run's session-position
 evidence may bear on it but cannot be patched into a pass.
- A flat ~90ms plateau would NOT license "known Emacs issue"
 language; it licenses "steady-state reached, magnitude mapped".
- Addendum 2026-08-26 (post-review): D's quasi-steady finish is
 NOT an ENCA credit.  B/C divergence marks an unexplained common
 factor (p0-tree builds / environment / slot) -> handed to Rider R
 of Phase 5 (bench/eipb/phase5/PHASE5.md section 4); this report
 grants no architecture or optimization license in any direction.
