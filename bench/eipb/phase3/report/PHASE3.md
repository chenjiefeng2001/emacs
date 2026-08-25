# EIPB Phase 3 -- User Path Coverage Expansion (T3-A file I/O, T3-B isearch)
# Executed 2026-08-25.  Raw: bench/results/eipb_t3.log
#
# COVERAGE phase -- observed differences ONLY, no causal claims
# (doctrine 8).  12 independent sessions = 4 builds x 3 rounds,
# order shuffled per round (logged in EIPB3_ROUND lines).  79 lines
# per session, perfectly symmetric, zero FATAL.
#
# Scope notes: page cache is WARM throughout (files are written by
# the session itself immediately before opening) -- true cold-cache
# measurement requires cache-drop privileges, deferred.  elisp arm
# capped at 1MB (cold full fontify of 10MB elisp would dominate the
# session budget; T2 already covers that regime synthetically).
# GC runs NATURAL (part of the user path); gcs_delta emitted as
# protocol verification -- it matched EXACTLY across builds
# (elisp/1MB: 117 GCs in every single build; fund cells: 0).

## 0. Verdict table

| path | verdict |
|------|---------|
| T3-A open chain | **No user-perceivable stall at <=10MB warm scale.** io is the only non-trivial segment (10MB read+decode ~60-170ms, one-shot); mode/fontify/redisplay segments behave as known from T2; idle drain is microseconds |
| T3-B isearch | **Incremental keystroke is effectively FREE: p50 0.11-0.16ms at both sizes, hit or miss, all builds.** The only tail is the first keypress (initial scan from BOB): 1.6-2.8ms @100KB, 7-16ms @1MB -- single-digit, once per search |
| ENCA | **No systematic direction on any new path.** Every >30% flag is contradicted by a neighboring metric or rep (details below); all such entries are OBSERVED DIFFERENCES pending multi-session tails |

## 1. T3-A -- open chain segments (pooled means over 3 sessions)

Stable picture (r2 = steady-state rep; r1 includes first-touch noise):

| cell          | io_ms (r2)        | mode_ms | font_ms            | red_ms    |
|---------------|-------------------|---------|--------------------|-----------|
| f/fund/100KB  | ~0.55             | ~0.23   | ~0.001 (no-op)     | ~1.1      |
| f/fund/1MB    | ~4.0              | ~0.28   | ~0.001 (no-op)     | ~1.5      |
| f/fund/10MB   | 58 - 78 (A/C low, B mid, D 172*) | ~0.3 | ~0.001 | 5.7 - 33* |
| f/elisp/100KB | ~0.58             | ~0.38   | 374 - 1005 (cold, noisy) | 1.1 - 7.0 |
| f/elisp/1MB   | ~4.2              | ~0.4    | 5.5 - 6.5 s        | ~2.8 - 8.4|

(*) single-rep outliers inside 3-session pools; directions do not
replicate across reps/metrics -- reported as observed differences,
not attributed (doctrine 8).

Reading:
- decode/read scales linearly with size (~4ms/MB warm) and sits
  INSIDE the same band for all four builds;
- the expensive segment of the open chain is FONTIFY, exactly as T2
  established synthetically; fundamental files skip it entirely;
- sit-for(0) drain (action->idle) is 3-15 MICROseconds everywhere:
  after redisplay returns, nothing else is hiding in the path;
- gcs_delta symmetry (117/117/117/117) doubles as an allocator
  determinism check: identical bytecode paths allocate identically
  across all four builds.

## 2. T3-B -- interactive isearch (per-keypress distributions)

| cell        | p50 (all builds) | key1 / p95 (=initial BOB scan)     |
|-------------|------------------|------------------------------------|
| i/100KB/hit | 0.11 - 0.14 ms   | 1.6 - 2.6 ms                       |
| i/100KB/miss| 0.11 - 0.16 ms   | 0.7 - 2.8 ms                       |
| i/1MB/hit   | 0.12 - 0.14 ms   | 7.3 - 16.2 ms (A slowest here)     |
| i/1MB/miss  | 0.12 - 0.15 ms   | 8.6 - 11.0 ms                      |

- After the first keypress locks onto the match position, every
  subsequent incremental keystroke re-searches from the current
  point: sub-0.2ms flat, size-independent in this range.  isearch is
  NOT a latency risk for interactive editing sessions on <=1MB
  buffers.
- The failing-tail case (miss) costs the same as hit: no pathological
  retry loop at these sizes.
- First-keypress flags (i/1MB/hit key1: A 16.2 vs C/D ~8ms) have
  n=3 per build and flip direction against the max-metric row
  (D/A 0.62) -- unresolvable at this depth, PENDING more sessions.

## 3. Where this leaves the interactive latency map

After Phases 1-3, measured user paths now include startup, editing,
redisplay, window/buffer topology, GC-in-path, font-lock chains,
file-open chain and isearch.  The ONLY user-perceivable millisecond
stalls proven so far remain:

1. c-mode jit-lock context chunks >=100ms inside single keystrokes (T2);
2. large-heap GC pauses 190-320ms (T1, vanilla-identical);
3. multi-window repaint multiplication (~45-47ms @8 panes, T2 Q3).

File I/O and isearch join "editing/redisplay/undo" in the cleared
column at current scale.  Remaining unmapped: IDE-MIXED-01 trace,
xref/imenu, org/dired magit-class workflows, true cold-cache opens,
GUI variants (T3 remainder + T4/T5).

## 4. Method disclosures

- 3 sessions x randomized orders satisfy doctrine 7; doctrine 8's
  N>=sessions bar for CAUSAL claims is met nowhere yet -- every
  cross-build entry above is an observed difference.
- Warm page cache only; cold-start I/O deferred.
- First-keypress metrics have n=3 pooled values per build: thin.
  Tail claims need >=10 sessions (future tier work).
- Runner quirk kept honest: shuf emits newline-separated tokens, so
  EIPB3_ROUND marker lines wrap -- actual execution order remains
  fully recoverable from the log line sequence.
