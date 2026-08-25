# EIPB Phase 3.3 -- dired coverage (T3 tail) + T3 CLOSURE
# Executed 2026-08-26.  Raw: bench/results/eipb_t33.log
#
# Minimal frozen set: empty/small(20)/big(2000) dir opens,
# refresh, create/rename/delete(+revert), dired<->file jump.
# 8 sessions = A/B/C/D x 2 shuffled rounds; 37 symmetric lines
# per session, zero FATAL, teardown clean.

## 0. Verdict

**A ~= B ~= C ~= D on every cell -- dired = mapped / no ENCA
signal.** One new mapped entry: entering a 2000-file directory
costs ~0.18-0.23 s one-shot (listing + render); everything else is
single-digit milliseconds.

## 1. Action percentiles (ms, pooled; format p50/p95/max)

| cell             | A              | B              | C              | D              |
|------------------|----------------|----------------|----------------|----------------|
| dd/open-big      | 234/269/269    | 177/208/208    | 222/287/287    | 226/267/267    |
| dd/open-small    | 9.9/38/38      | 9.8/38/39      | 9.7/39/39      | 10.1/46/46     |
| dd/open-empty    | 29.5/680*/680* | 24.9/483*/483* | 30.3/573*/573* | 20.9/545*/545* |
| dd/refresh-small | 7.1/45.9/45.9  | 7.3/29.9/29.9  | 7.5/41.2/41.2  | 8.4/42.1/42.1  |
| dd/create        | 8.4/40.4/40.4  | 9.4/32.5/32.5  | 8.6/33.6/33.6  | 9.3/46.0/46.0  |
| dd/rename        | 7.9/8.9/8.9    | 8.4/9.5/9.5    | 9.0/12.5/12.5  | 8.6/14.8/14.8  |
| dd/delete        | 8.3/36.0/36.0  | 8.5/40.7/40.7  | 8.3/39.2/39.2  | 8.4/41.4/41.4  |
| dd/jump          | 2.30/9.6/9.6   | 2.27/4.9/4.9   | 2.27/6.0/6.0   | 2.42/11.8/11.8 |

(*) empty-dir p95/max = FIRST-open cold start of the whole dired
machinery (ls subprocess spawn + mode setup), identical shape in
all four builds -- session-position artifact, not a path property;
the p50 row is the honest steady state.

## 2. Reading

- Directory-entry scaling is linear-ish and modest: 20 files ~10 ms,
  2000 files ~200 ms.  Only users living in huge flat directories
  feel this, and it is one-shot per visit, vanilla-identical.
- Mutation ops (create/rename/delete incl. revert+render) all sit at
  ~8-9 ms p50 -- sub-frame, no scaling concern.
- No ENCA direction anywhere; doctrine 8 bars causal language at
  n=6/build anyway.

## 3. T3 CLOSURE -- latency atlas FROZEN (v1)

With 3.3 landed, T3 user-path coverage is complete:

| path                     | state     | magnitude                  |
|--------------------------|-----------|----------------------------|
| buffer edit / undo       | closed    | us                          |
| isearch incremental      | closed    | <ms                         |
| file open chain          | closed    | io ~4ms/MB; fontify dominates |
| LSP transport            | closed    | EVS-4.x                     |
| completion transport     | closed    | EVS-5.x                     |
| IDE mixed composition    | validated | sum of parts                |
| xref project scan        | mapped    | ~70-100 ms                  |
| capf completion round    | mapped    | ~45-70 ms                   |
| dired all ops            | mapped    | ~8-10 ms; big-dir ~200 ms   |
| org subtree cycle / nav  | closed    | ~5 ms / ~1 ms               |
| imenu cold index         | STALL     | ~300-560 ms one-shot        |
| org global sweep         | STALL     | ~0.5 s @2000 headings       |
| multi-window repaint     | STALL     | ~45-47 ms @8 panes          |
| c-mode jit chunks        | STALL     | >=100 ms                    |
| large-heap GC pause      | STALL     | 190-320 ms                  |

Known unmapped (explicitly deferred, not "to do soon"): true
cold-cache opens, GUI variants, magit-class flows.

Phase 4 gate: SOAK must answer whether the CLOSED rows stay closed
and the STALL rows stay stable over 30 min / 2 h -- including the
unexplained kill-emacs exit hang (p32), which is now a REQUIRED T4
attribution item, not a footnote.

## 4. Harness note

let-vs-let* ordering bug caught by byte-compile warning + immediate
smoke FATAL (void-variable root) -- fixed before any measurement.
dired-goto-file used for exact jump positioning (first listing lines
may be "." / ".." entries).
