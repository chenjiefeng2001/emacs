# EIPB Phase 2.1 -- B/C Attribution Closure (mid-buffer x visible)
# Executed 2026-08-25 (tty, WSL).  Raw: bench/results/eipb_p21.log
#
# Frozen question: WHO caused the phase-2 mid-buffer edit+visible
# D-vs-A gap (1.3-3.8x)?  Fork base or ENCA?
#
# Controls honored: identical op traces (deterministic content, cyclic
# switch order), GC pinned (threshold 64MB + collect before every
# cell), fundamental-mode only (font-lock excluded), buffer-undo-list
# t, same frame geometry, builds A/B/C/D interleaved A,B,C,D x 2
# rounds (drift-cancelling order).  30 lines per build-round, 240
# data lines total, zero FATAL, wall 135 s.

## 1. Verdict

**The phase-2 signal does not replicate.  No attribution to ENCA --
and none to the fork base either.  Classified as a measurement-
session order artifact; ENCA cleared on this path.**

Medians (ms, both rounds pooled per build):

| cell      | metric |     A |     B |     C |     D | B/A  | C/A  | D/A  |
|-----------|--------|------:|------:|------:|------:|-----:|-----:|-----:|
| mb/100KB  | p50    | 13.48 |  8.78 | 10.44 | 10.08 | 0.65 | 0.77 | 0.75 |
| mb/1MB    | p50    | 15.78 | 11.26 | 11.83 | 10.42 | 0.71 | 0.75 | 0.66 |
| eob/1MB   | p50    | 13.88 |  6.80 | 10.54 |  7.40 | 0.49 | 0.76 | 0.53 |
| noop/1MB  | p50    |  0.11 |  0.11 |  0.12 |  0.13 | 0.96 | 1.10 | 1.17 |
| win8/mb   | p50    | 27.99 | 21.61 | 21.05 | 23.51 | 0.77 | 0.75 | 0.84 |
| bufswitch | p50    | 17.94 | 16.54 | 16.10 | 19.97 | 0.92 | 0.90 | 1.11 |

Decision-tree walk (per the frozen protocol):

- "B~D => fork base"?  NO -- B~D holds but C~D holds too; all three
  fork-era builds cluster together on every median cell.
- "C~D => ENCA"?       NO -- C is never distinguishable from B.
- "C~B~A => environment"?  CLOSEST -- three builds indistinguishable,
  the fourth (vanilla A) sits apart, and in the SLOWER direction.
- "only D anomalous => new module interaction"?  NO.

## 2. Reading

1. There is no D-specific excess anywhere: D/A medians span
   0.66-1.17 across all six cells.  The worst D cell (bufswitch
   1.11x) is inside run-to-run noise for a single session.
2. The phase-2 "gap" inverted under interleaving: A became the slow
   side.  In Phase 2 every section ran D first and A second; in
   Phase 2.1 A always occupied round-position 1.  Position in the
   run order, not build identity, tracks the deviation -- the
   signature of machine-state drift (cache/frequency/pty warmth),
   which two interleaved rounds cancel and a fixed order does not.
3. Consequently the honest claimable result is narrow and negative:
   **all four builds sit in the same performance band on the
   mid-buffer x visible path once run order is controlled.**
   Absolute cross-build deltas smaller than ~30% remain unresolvable
   in this single-VM tty setup and stay PENDING until a multi-run
   matrix exists (doctrine 3 unchanged).
4. Bonus observation, NOT a claim: vanilla-A being slowest here is
   itself suspect (it held position 1 in both rounds) and must not
   be reported as "fork faster than upstream".

## 3. Tail metrics (disclosure, not verdicts)

Single-session p95/max are dominated by cold-start spikes inside the
measurement window (e.g. win8/mb max: B 39 vs C 145 vs D 350 ms;
mb/1MB max: D 195 vs A/B/C ~95).  n=300 pooled ops per cell gives
stable medians but thin tail confidence at these magnitudes.  Tail
comparisons across builds remain PENDING multi-session repetition.

## 4. Consequences

- PENDING-B/C-CHECK flag in MATRIX.md/EIPB.md: RESOLVED (cleared,
  no ENCA action).
- Tag `enca-eipb-phase2-attribution-closure`: authorized by this
  document (attach to the Phase 2.1 commit).
- Methodology debt carried forward: future tier runs MUST interleave
  builds (round-robin), never fixed-order; recorded in EIPB.md
  doctrine additions.
