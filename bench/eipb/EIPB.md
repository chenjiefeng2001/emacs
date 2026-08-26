# EIPB -- Emacs Integrated Performance Benchmark
# (master plan + contract, created 2026-08-25; project mainline going forward)

## 0. Mission

> Establish systematic, percentile-based, multi-build performance
> measurement across ALL interactive Emacs domains, so that "modern
> IDE parity" becomes a claim backed by a matrix instead of a single
> synthetic path.

This framework exists because EVS-1..5 proved discipline on ONE path
(completion/LSP) while 20+ other interactive domains were never
measured.  From now on, EVERY performance decision in this project
must cite EIPB data.  It is not a victory lap; it is the instrument.

## 1. Non-negotiable doctrine

1. **No composite score. Ever.** Layered metrics only; cross-domain
   aggregation is forbidden (a single number hides interaction stalls).
2. **Percentiles mandatory**: every latency metric reports
   p50/p95/p99/p99.9/max.  Means are never primary evidence.
3. **Multi-build doctrine**: every cell eventually runs under
       A = upstream vanilla
       B = fork, ENCA disabled (ENCA_OBJ empty)
       C = fork, ENCA enabled (same tree as B)
       D = fork, ENCA enabled, CURRENT feature set (~/enca-p11)
   An "ENCA impact" claim requires the relevant row under A/B/C(/D).
   Until then the matrix cell says PENDING, never estimated.
4. **tty / GUI separated.** GUI sessions are deferred until a GUI
   environment exists; tty numbers must never be labeled as GUI.
5. **Attribution gate unchanged** (ARCHITECTURE.md section 26): EIPB
   identifies WHERE milliseconds live; it does not authorize
   architecture changes by itself.
6. **Long-run stability is a first-class metric**: latency(t->inf)
   ~= latency(0) is an acceptance criterion, checked in T4 soak runs.
7. **Round-robin build order mandatory** (added 2026-08-25, Phase
   2.1): builds are interleaved (e.g. A,B,C,D x N rounds), never run
   fixed-order -- a fixed order produced a phantom 1.3-3.8x "D gap"
   that vanished under interleaving.  Single-session cross-build
   deltas below ~30% are UNRESOLVABLE in the current tty/WSL rig and
   must be reported as PENDING, never as regressions.
8. **Attribution gate, strengthened** (added 2026-08-25): a causal
   report "difference caused by component X" requires ALL of:
   same workload + controlled machine state + randomized build
   order + >=N independent sessions + effect size above the noise
   band.  Anything less may ONLY report "observed difference";
   causal language without the full set violates this gate.

## 2. Coverage map (status 2026-08-25)

    Domain                 coverage   source
    ---------------------  --------   --------------------------
    Completion             █████      EVS-5.x (this repo)
    LSP transport          █████      EVS-4.3/EVS-5.4
    Buffer editing         ███░       P0 baseline + EIPB T1
    Redisplay (tty)        ██░░       EVS-4.4 + P0 tty + EIPB T1
    Search/regexp          ███░       P0 batch + T1 sweeps + T3-B isearch
    Diagnostics            ██░░       EVS-4.3 storm-real
    Large-file editing     ███░       P0 1MB + T1 + T3-A open-chain
    GC                     ██░░       P0 gc-full + T1 + T2 Q2 (user-path)
    Startup                █░░░       ad-hoc -> T1 formalizes
    Undo/redo              ░░░░       -> T1
    Lisp evaluation        █░░░       P0 sort/string cells only
    Lisp compilation       ░░░░       deferred (T2)
    Syntax/font-lock       ███░       T2 Q1 chain measured (c/org/elisp/
                                      python/plain, 100KB-10MB)
    File I/O               ███░       T3-A open chain (io/decode/mode/
                                      fontify/redisplay/idle)
    Multi-buffer/window    ███░       T2 Q3 measured (win1-8, buf10-100,
                                      spot8x100)
    xref/imenu/eglot       ████       T3.2 imenu/xref measured; eglot
                                      covered by EVS-4.x -- CLOSED
    Org/Dired/Magit/Term   ███░       org + dired measured (T3.2/3.3);
                                      magit/term deferred by decision
    Mixed workload         ░░░░       T3 (IDE-MIXED-01 trace)
    Soak 30m/2h            ░░░░       T4
    External IDE ref       ░░░░       T5 (last; same-trace rule)

## 3. Tiers and phases

### T1 -- Core Interactive (PHASE 1, this document's deliverable)
Cells (all percentile-reported):

    STARTUP   batch x3 wall; tty cold+warm wall;
              in-process milestones (first-lisp, first-buffer,
              first-redisplay)
    EDITING   sizes {64KB, 1MB, 10MB} x
              {ins-1B@eob, ins-1B@mid, del-1B@mid, paste-1KB,
               typing-x100}; keypress->buffer-state percentiles;
              visible variant (forced redisplay) on 1MB
    REDISPLAY R0 idle-noop, R1 1-line change, R2 popup overlay,
              R3 100-site change; 1MB buffer; tty
    GC        forced-garbage-collect pause distribution (n=200);
              allocation storm (wall, GC count, cumulative gc-elapsed)
    SEARCH    1MB & 10MB: literal sweep, identifier-regexp sweep,
              alternation-regexp sweep
    UNDO      200 inserts then 200 undos on 1MB; per-op percentiles

Acceptance: results recorded under A/B/C/D; matrix filled; no cell
depends on network or GUI.

### T2 -- Services Integration (Phase 2)
font-lock keypress->fontify->redisplay->visible chain; file open/save
ladder 1KB..100MB (warm/cold noted); multi-buffer {1,10,100,1000} x
multi-window {1,2,4,8} switch/edit/search; isearch interactively
scripted; imenu/xref basic latencies; batch byte-compilation of a
fixed corpus.

### T3 -- Mixed Workload (Phase 3)
IDE-MIXED-01: scripted end-to-end session (open project, 20 files,
switch, type, complete, LSP, scroll, search, edit, undo, re-complete,
switch window, save) with per-action timestamped trace; derived
user-path metrics (action->visible, action->idle).

### T4 -- Soak (Phase 4)
SOAK-30M / SOAK-2H running T3-style mixed load; track RSS, heap,
GC pause drift, latency drift; acceptance: p99(t_end) <= 2x p99(t_0)
and no monotonic growth in RSS after warmup.  Additional frozen
metric (added 2026-08-26): **tail amplification ratio** =
p99(last 10 min) / p99(first 10 min) per action class -- long-run
failures usually show as tail growth with flat medians (cache
non-eviction, snapshot retention, fragmentation, GC pressure).

### T5 -- External reference (Phase 5, LAST)
Same-trace comparison against VS Code/JetBrains on identical scripted
workloads; user-perceivable paths only; no internal-implementation
comparisons.  Explicitly out of scope until T1-T4 exist.

## 4. Metrics dictionary

    keypress->buffer-state : insert/delete return to caller
    keypress->visible      : op + forced redisplay completion
    action->idle           : op + sit-for(0) drain
    GC pause               : duration of one garbage-collect
    RSS                    : process resident set (shell side, psr)

## 5. Layout & usage

    bench/eipb/EIPB.md        this plan
    bench/eipb/eipb_core.el   T1 harness (runs inside tty emacs)
    bench/eipb/eipb_startup.el startup milestone probe
    bench/eipb/eipb_run.sh    orchestrates builds A/B/C/D -> results
    bench/eipb/MATRIX.md      performance matrix (filled incrementally)
    bench/results/eipb_t1.log raw output

Line format:

    EIPB|<build>|<tier-cell>|<metric>|<value>

## 6. Status

    [x] Phase 1 (T1) designed + executed 2026-08-25 (REPORT section 30)
    [x] Phase 2 (T2) attribution phase -- contract below, executed
        2026-08-25 (REPORT section 31; verdict table
        bench/eipb/phase2/report/PHASE2.md)
    [x] Phase 2.1 B/C attribution -- mid-buffer signal CLEARED
        (session order artifact, not ENCA; REPORT section 32,
        bench/eipb/phase2/report/PHASE2_1.md)
    [x] Phase 3 (T3) user-path coverage CLOSURE -- T3-A file open
        chain + isearch (REPORT 33); IDE-MIXED-01 (REPORT 34);
        xref/imenu + org (REPORT 35); dired + atlas freeze v1
        (REPORT 36, 2026-08-26).  Deferred by decision: cold-cache
        opens, GUI variants, magit-class flows.
    [x] Phase 4 (T4) -- 30M leg executed 2026-08-26 (REPORT 37,
        bench/eipb/phase4/report/PHASE4_1.md): G2/G3/G4 pass x4;
        G1 A/D/B pass, C 2.15 marginal-fail UNRESOLVABLE (absolute
        tails converge; doctrine 7).  New universal finding:
        insert-typing medians grow ~4x/30min in ALL builds incl
        disabled-B -> saturate-vs-unbounded is SOAK-2H's question.
        2H leg executed 2026-08-26 (REPORT 38,
        bench/eipb/phase4/report/PHASE4_2.md): saturation answer
        NEGATIVE -- drift persists at hour 2, C/B accelerating,
        A creeping, only D quasi-steady (~70 ms); disabled-B worst
        end-state => long-session degradation candidate stands,
        ENCA exonerated again.  G2/G3 pass x4 (p32 hang 8/8 clear);
        ta/overall D 1.26, C/A/B 3.51/3.75/5.88 FAIL-as-measured
        UNRESOLVED (tails leave the T4.1 band; position-confounded;
        doctrine 3 multi-session next).
    [ ] Phase 5 (T5)

## 7. Phase 2 contract -- tail-latency attribution (frozen 2026-08-25)

Phase 2 is NOT "more benchmarks".  It answers three frozen questions
about WHERE user-perceivable stalls originate in Emacs core services.
Measurement and attribution only: no redisplay/GC/ENCA architecture
changes are authorized by this phase.

### Q1 -- Is font-lock an editing tail-latency source?

Full user chain measured per keystroke:

    keypress -> buffer change -> fontification -> redisplay -> visible

Two arms per cell:
  NATURAL : insert + (redisplay t); jit-lock runs as it would for a
            real user (visible region fontified inside redisplay).
  DECOMP  : insert; (jit-lock-fontify-now around-point) timed;
            then (redisplay t) timed.  Segments attributed separately.

Languages: c-mode, emacs-lisp-mode, python-mode, org-mode,
fundamental (control).  Sizes: 100KB + 1MB all languages; 10MB for
c-mode + fundamental only (runtime cap, documented scope cut;
100MB deferred to T3).  Point kept on-screen so jit-lock cannot
defer work to stealth timers.

Metrics: buffer_ms / fontify_ms / red_ms / total_ms distributions;
stall counters (ops >10ms / >50ms / >100ms); gcs-done delta per cell.

### Q2 -- Does GC pause actually enter the user-perceivable path?

Protocol: 1000 single-char keypress->visible ops at EOB of a fresh
1MB fundamental buffer (font-lock excluded by design).  Arms:

  NOGC     gc-cons-threshold raised + pre-GC; expect zero GCs
  NATURAL  gc-cons-threshold = 20000; GCs fire naturally mid-typing
  FORCED   explicit (garbage-collect) every 25th op, INSIDE the
           timed window

Verdict rule: if NATURAL/FORCED tails (p99/max, stall counts) exceed
NOGC materially, GC IS a user-path stall source with magnitude =
delta.  If pauses land only outside interactive windows, priority
drops.  gcs-done delta emitted per arm as protocol verification.

### Q3 -- Do multi-buffer / multi-window change redisplay behavior?

tty session; window scaling capped at 8 (16 exceeds sane tty width;
documented cut).  Matrix sampling:

  windows {1,2,4,8} x fixed 1 buffer:
      noop-redisplay, edit+visible, popup+visible, scroll x10
  buffers {1,10,100} x fixed 1 window:
      switch-buffer cycle (each switch timed), edit+visible
  spotlight: 8 windows x 100 buffers:
      edit+visible, popup+visible, switch cost

(memory-limit) KB emitted at each section boundary.

### Products

    bench/eipb/phase2/eipb_p2_core.el   harness (FL / GCPATH / WB)
    bench/eipb/phase2/eipb_p2_run.sh    runner
    bench/eipb/phase2/report/PHASE2.md  Q1-Q3 verdict table
    bench/results/eipb_t2.log           raw lines (EIPB2|...)

Line format: EIPB2|<build>|<cell>|<metric>|<value>
