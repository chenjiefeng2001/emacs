# EIPB Phase 3.1 -- IDE-MIXED-01 Scripted Session Trace
# Executed 2026-08-25.  Raw: bench/results/eipb_t31.log
#
# Frozen script: open project(20 files) -> switch -> type(c-mode jit)
# -> complete(capf) -> scroll -> isearch -> edit/del -> undo ->
# re-complete -> window cycle -> save -> idle.  Per-action trace,
# log order = chronology.
#
# COVERAGE phase: observed differences only (doctrine 8).  8 sessions
# = 4 builds x 2 rounds, shuf-randomized orders (r1: D A B C;
# r2: A D C B).  205 symmetric lines/session, zero FATAL, zero
# completion errors.

## 0. Verdict

**Mixing does NOT expose new combined bottlenecks.** Every action
median sits at or below its single-path counterpart from Phases 1-3,
and the known stalls appear exactly where predicted (c-jit typing
tails, seconds-scale cold fontify on c-file opens) -- and nowhere
else.  The map gains one new coverage entry: native completion-at-
point rounds run ~45-70 ms p50 (Completions-window render included),
the largest steady interactive latency measured outside GC/fontify.

## 1. Action medians (ms, pooled raw ops over 2 sessions/build)

| action        | A      | B      | C      | D      | reading                          |
|---------------|--------|--------|--------|--------|----------------------------------|
| typevis (c)   | 17.4   | 12.1   | 10.6   | 10.0   | matches T2 c-mode chain          |
| capf round    | 68.3   | 46.4   | 55.0   | 58.4   | NEW entry: ~50ms typical         |
| open (mixed)  | 10.2   | 11.1   | 10.3   | 17.5*  | p50 small; tails dominated below |
| switchbuf     | 2.74   | 2.58   | 2.60   | 2.35   | flat                             |
| scroll        | 2.30   | 2.37   | 2.38   | 2.44   | flat                             |
| editdel       | 0.93   | 0.93   | 0.90   | 0.87   | us-level                         |
| undostep      | 0.21   | 0.21   | 0.22   | 0.21   | us-level mid-session             |
| isearchkey    | 0.89   | 0.85   | 0.93   | 2.53*  | steady keys free                 |
| save          | 10.4   | 6.3    | 6.2    | 6.7    | sub-frame                        |
| idle drain    | 0.22   | 0.19   | 0.19   | 0.20   | nothing hidden                   |

(*) single-session-position noise; directions do not replicate
across neighboring metrics (doctrine 8: observed differences).

Session wall (mean of 2): A 34.4s / B 28.1s / C 28.9s / D 29.5s.
A sits +18% above the fork builds with n=2 and held early positions
in both rounds (pos 2, pos 1); inside the rig noise band -- reported
as observed difference ONLY.

## 2. Tails (thin-n disclosure)

Pooled n per build/action = 40 (typevis/open/scroll/editdel) down to
4-10 (capf2/isearchkey/save/idle/wincycle).  At this depth:

- typevis p95 ~93-123 ms in ALL FOUR builds = the known T2 c-mode
  jit chunk hits appearing naturally inside a mixed session; max up
  to ~636 ms = jit+GC overlap blips, both-direction across builds;
- open p95 3.6-5.3 s / max 14-20 s = cold FONTIFY of the project's
  c files on first open (T2/T3-A consistent), not I/O;
- isolated spikes (isearchkey B 664ms once; wincycle D 53ms once;
  capf2 C 339ms once) are single-value outliers at n<=10 -- no
  cross-build pattern, all PENDING multi-session tails.

No tail category shows an ENCA-specific or fork-specific signature.

## 3. Map consequences

- IDE-MIXED-01 validates composition: real sessions cost what their
  parts cost; no emergent interaction stall found at this scale.
- Completion enters the map as a ~50 ms user-visible operation
  (native capf incl. Completions render).  This is a COVERAGE datum,
  not a defect verdict; LSP-class backends were measured separately
  (EVS-4.x/5.x).
- Remaining unmapped after 3.1: org folding/export workflows,
  xref/imenu command latencies, dired, true cold-cache opens,
  GUI variants, soak drift (T4).

## 4. Harness postmortem (all smoke-proven)

1. kill-buffer PROMPTS on modified file-visiting buffers -> scripted
   session hung to timeout despite summary emitted.  Fix: clear
   modified flags + unlock + silence query functions before kills.
2. primitive-undo consumes a whole boundary-delimited segment per
   call; scripted ops had NO boundaries so one call ate everything
   (undostep n=1).  Fix: eipb3--op appends undo-boundary per op --
   which also matches real per-command undo granularity.
3. dabbrev-expand abandoned for the completion stand-in: repeating
   the same prefix after context scrub drives its internal cycling
   state into raw search-failed / wrong-type-argument signals (two
   smoke iterations of evidence).  Native completion-at-point is
   stateless across calls and exercised instead.
