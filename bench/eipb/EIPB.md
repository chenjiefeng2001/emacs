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

## 2. Coverage map (status 2026-08-25)

    Domain                 coverage   source
    ---------------------  --------   --------------------------
    Completion             █████      EVS-5.x (this repo)
    LSP transport          █████      EVS-4.3/EVS-5.4
    Buffer editing         ███░       P0 baseline + EIPB T1
    Redisplay (tty)        ██░░       EVS-4.4 + P0 tty + EIPB T1
    Search/regexp          ██░░       P0 batch + EIPB T1
    Diagnostics            ██░░       EVS-4.3 storm-real
    Large-file editing     █░░░       P0 1MB only -> T1 extends
    GC                     █░░░       P0 gc-full -> T1 extends
    Startup                █░░░       ad-hoc -> T1 formalizes
    Undo/redo              ░░░░       -> T1
    Lisp evaluation        █░░░       P0 sort/string cells only
    Lisp compilation       ░░░░       deferred (T2)
    Syntax/font-lock       ░░░░       deferred (T2; keypress->
                                      fontify->redisplay->visible)
    File I/O               ░░░░       deferred (T2)
    Multi-buffer/window    ░░░░       deferred (T2)
    xref/imenu/eglot       ░░░░       deferred (T2/T3)
    Org/Dired/Magit/Term   ░░░░       deferred (T3)
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
and no monotonic growth in RSS after warmup.

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
    [ ] Phase 2 (T2)
    [ ] Phase 3 (T3)
    [ ] Phase 4 (T4)
    [ ] Phase 5 (T5)
