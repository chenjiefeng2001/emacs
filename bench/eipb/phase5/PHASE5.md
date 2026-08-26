# EIPB Phase 5 -- external same-trace reference + long-session
# attribution (T5).  Contract frozen 2026-08-26, BEFORE any
# measured byte.  Amends EIPB.md section "T5 -- External reference".
#
# Directive (2026-08-26, post-T4.2 review):
#   - D's quasi-steady SOAK-2H finish is NOT read as an ENCA credit;
#   - mainline enters Phase 5 in two layers (P5-1 same-trace
#     matrix, P5-2 explainable-path comparison);
#   - the T4.2 open question (why do B/C accumulate hundreds of ms
#     of typing drift over 2h while A/D stay near-steady) rides
#     along as Rider R with a minimal attribution ladder;
#   - NO runtime/ENCA/architecture changes are authorized by any
#     Phase 5 work.
#
# Project identity note: with T1-T4 closed, the program operates as
# an evidence-chained Emacs interactive-performance ATTRIBUTION
# platform.  Continued ENCA optimization currently has no
# supporting evidence.

## 0. Scope and prohibitions

Measurement and attribution only (unchanged since T2): no
redisplay/GC/allocator/ENCA architecture changes authorized.
Composite scores banned (EIPB discipline); percentiles mandatory:
p50/p95/p99/max + n + tail-amplification ratio per class (T4
wall-clock bucket convention).  false_hit = 0 hard gate unchanged
wherever the completion engine is exercised.

## 1. P5-0 -- instrumentation probe (feasibility gate)

No external IDE is measured until a probe demonstrates, on THAT
IDE, the ability to emit segment-timestamped trace lines for one
scripted session without altering the IDE's semantics:

    EIPB5|<subject>|<cell>|<metric>|<value>

Subjects: A/B/C/D (existing emacs rigs) + one per external IDE
(VSCODE / JETBRAINS / ...).

Probe exit criteria per external subject:
  a) segment boundaries emitted for the section-2 path chain;
  b) trigger/cache/display semantics DOCUMENTED by the probe run
     itself (what fired the request, what state was warm, when a
     result counts as visible);
  c) raw artifacts archived under bench/eipb/phase5/probe/.
Failure on (a)-(c) => subject recorded OUT-OF-SCOPE with reason;
no comparative sentence may involve it thereafter.

## 2. P5-1 -- same-trace matrix (user-path decomposition)

Frozen segment chain, per op:

    keypress -> request -> backend -> result -> visible

Segment names: input-commit / request-emit / backend-turnaround /
result-integrate / display-commit.  Each timed where the subject
exposes the boundary; otherwise N/A WITH REASON -- never guessed,
never merged silently.

Workload rows (frozen set; every row x every enrolled subject):

    W1 steady typing          sustained insert burst
    W2 completion HIT         warm/cached candidate path
    W3 completion MISS        cold backend round-trip
    W4 edit-after-completion  editing resumed post-popup
                              (Stage-D evidence row)
    W5 large-file local edit  ~1MB buffer, local-region ops
    W6 session-age pair       identical W1-W5 cells re-run at t0
                              and t+~2h inside ONE session
                              (direct hook for Rider R)

Stats per cell: p50/p95/p99/max/n + tail amplification (first vs
last 10-min buckets).  Multi-subject rig days use randomized
order; rig-state notes (uptime, background load) mandatory per
block -- T4.2 showed slot effects are real.

## 3. P5-2 -- explainable-path comparison only

A cross-subject comparison SENTENCE is licensed only when both
paths match on ALL of:

  - trigger semantics       (keystroke vs idle-timer vs explicit)
  - cache/prefetch state    (warm/cold, probe-documented)
  - integration semantics   (incremental inline vs atomic popup)
  - display-commit meaning  (first paint vs full paint; tty emacs
    vs GPU-composited GUI is a PERMANENT mismatch here)

Unlicensed pairs are recorded side-by-side labeled
OBSERVED-NOT-COMPARABLE: no ranking language, no derived ratios.
Forbidden example: Emacs(+clangd) 78-92ms candidate-ready vs any
IDE's monolithic "completion latency" number.

Expected cleanest licensed segment: backend-turnaround
(request->result) between subjects sharing clangd.  P5-2 leans
there, not on end-to-end walls.

## 4. Rider R -- B/C long-session degradation, minimal attribution

Question (T4.2): why do B/C degrade by hundreds of ms of typing
median over 2h while A/D do not?  Leading co-observations
(PHASE4_2 sections 1/3): per-window GC-count collapse in exactly
C (959->56) and B (3402->80) while memlimit ratchets in ALL four
builds; C's w31-32 regime transition.

Ladder (cheapest first; each rung exits with a written note):
  R1 re-read existing artifacts: per-window gc-cons-threshold /
     memory-limit / gcs-done reconstruction for A-D from t41+t42
     raw logs; zero new runs if they suffice.
  R2 short-soak intervention arm (T2-Q2 NOGC/NATURAL precedent):
     20-min soak, GC pacing pinned-high vs natural, arms x
     {A, B, C} -- attribution manipulation, NOT an optimization.
  R3 single-variable build bisect ONLY IF R1-R2 implicate the p0
     trees: fresh-checkout rebuild of one p0 tree vs its measured
     twin; still zero source edits.
  R4 cross-check with P5-1 W6: same-shaped drift in an external
     subject => workload/backend-layer phenomenon; fork chase
     CLOSES with that note.
Decision rule: any rung explaining the split within noise ends the
rider; unexplained after R3 => question stays OPEN and parked,
never blocking the mainline.

## 5. Products

    bench/eipb/phase5/PHASE5.md          this contract
    bench/eipb/phase5/probe/             P5-0 feasibility artifacts
    bench/eipb/phase5/eipb_p5_core.el    emacs-side trace harness
    bench/eipb/phase5/eipb_p5_run.sh     multi-subject runner
    bench/eipb/phase5/report/PHASE5_*.md verdicts (post-execution)
    bench/results/eipb_t51*.log          raw lines (EIPB5|...)

## 6. Reading discipline

- Doctrine 8 everywhere; session-position effects assumed present
  until interleaving proves otherwise (T4.2 lesson, MATRIX 4.2).
- OBSERVED-NOT-COMPARABLE is a first-class outcome; a ranking
  sentence without a section-3 license is a protocol violation.
- Rider R stays observational until a rung yields a controlled
  delta; then magnitude language only.
- Negative results are products: OUT-OF-SCOPE findings and closed
  riders are recorded with the same care as passes.
