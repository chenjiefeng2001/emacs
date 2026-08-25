# EIPB Phase 3.2 -- xref/imenu command latency + org workload
# Executed 2026-08-26.  Raw: bench/results/eipb_t32.log
#
# COVERAGE phase -- observed differences only (doctrine 8).
# 8 sessions = 4 builds x 2 rounds, shuf-randomized orders
# (r1: B?/A/D/C recorded in log; r2: C D A B).  47 symmetric lines
# per session, zero FATAL, zero completion/index errors.
#
# Protocol determinism: xr/scan matches_mean = 1068.0000 in ALL FOUR
# builds (grep over 14 files, 1068 hits) -- byte-identical backend
# behavior.

## 0. Verdict table

| path | verdict |
|------|---------|
| imenu | **Cold index build is a REAL one-shot command latency: ~300-560 ms** (elisp 30KB / c 20KB); cached rebuilds are ~0.25 ms.  Navigation (goto+visible) free at 0.21-0.42 ms |
| xref project scan | grep-backed scan of small project = **67-98 ms p50** (subprocess spawn dominates); all builds same band, D/A spread unresolvable at n=6 |
| org subtree cycle | **~4.3-5.0 ms p50** -- folding interaction is CHEAP even on 2000 headings |
| org global visibility sweep | **~0.42-0.56 s p50, p95 up to 1.2 s -- NEW USER-PERCEIVABLE STALL ENTRY**, identical across all four builds (Emacs-core property, like GC) |
| org nav (outline-next-heading) | 0.8 ms p50; occasional 7-17 ms blips |
| org cold fontify (300KB) | 1.7-3.2 s one-shot, consistent with T2 org scaling |

## 1. Action percentiles (ms, pooled raw ops; format p50/p95/max)

| cell          | A                | B                | C                | D                |
|---------------|------------------|------------------|------------------|------------------|
| im/elisp/index| 0.27/431/431     | 0.25/563/563     | 0.24/490/490     | 0.26/562/562     |
| im/c/index    | 0.25/295/295     | 0.27/514/514     | 0.26/447/447     | 0.25/447/447     |
| im/goto       | 0.23/0.27/0.27   | 0.21/0.26/0.26   | 0.23/0.27/0.27   | 0.23/0.42/0.42   |
| xr/scan       | 66.8/226/226     | 90.3/255/255     | 82.9/239/239     | 97.6/218/218     |
| org/cycle     | 4.67/14.2/15.1   | 4.32/5.6/7.4     | 4.52/5.3/5.5     | 4.97/11.7/15.2   |
| org/global    | 500/966/966      | 423/913/913      | 499/1083/1083    | 561/1197/1197    |
| org/nav       | 0.81/10.3/14.4   | 0.81/7.5/8.0     | 0.86/8.4/8.5     | 0.87/10.0/16.6   |

org/fontify cold whole-buffer (single shot per session, mean of 2):
A 1744 / B 2740 / C 3161 / D 1897 ms -- n=2, inside noise band,
observed differences only.

## 2. Map reading

1. **imenu joins the stall list as a one-shot command latency**
   (~0.3-0.56 s first index on a modest file; scales with mode's
   generic-expression complexity and buffer size).  Cached repeats
   are free -- the cost is PER BUFFER FIRST USE.
2. **org/global-cycle enters at the same tier as multi-window
   repaint (~0.5 s)**: whole-buffer visibility recalculation +
   refontify sweep over 2000 headings.  Vanilla-identical => Emacs-
   core property, candidate for future core-side work only.
3. **Interactive org editing is safe**: subtree cycles and heading
   navigation stay in single-digit ms.
4. **ENCA: no direction anywhere.**  Every cross-build spread is
   either <30% band or contradicted by its neighbor metric;
   doctrine 8 bars any causal language at n=2 sessions.

## 3. Updated latency atlas (after Phases 0-3.2)

| path                      | state        | magnitude            |
|---------------------------|--------------|----------------------|
| buffer edit / undo        | closed       | us                   |
| isearch incremental       | closed       | <ms                  |
| file open chain           | closed       | io ~4ms/MB; fontify dominates |
| completion transport      | closed       | EVS                  |
| IDE mixed composition     | validated    | sum of parts         |
| completion capf round     | mapped       | ~45-70 ms            |
| xref project scan         | mapped       | ~70-100 ms           |
| imenu cold index          | **stall**    | ~300-560 ms one-shot |
| org subtree cycle/nav     | closed       | ~5 ms / ~1 ms        |
| org global sweep          | **stall**    | ~0.5 s @2000 heads   |
| c-mode jit chunks         | **stall**    | >=100 ms             |
| large-heap GC pause       | **stall**    | 190-320 ms           |
| multi-window repaint      | **stall**    | ~45-47 ms @8 panes   |

Still unmapped: dired, magit-class flows, true cold-cache opens,
GUI variants, soak drift (T4).

## 4. Harness postmortem (all probe-proven)

1. `xref-matches-in-directory` FILES argument is FIND-GLOB semantics,
   space-separated: ".*" silently matches DOTFILES ONLY (0 hits);
   "*" or "*.c *.el" work as intended.  Probe chain: minimal case ->
   raw *xref-grep* buffer -> manual pipeline -> glob semantics.
2. Manual replication of the grep pipeline missed the function's
   internal `<C> -> <C> -E` template rewrite; BRE `\[` then matched
   literal brackets -- probe artifact worth remembering.
3. Session hung INSIDE kill-emacs after every teardown step had
   logged clean (process-list empty!).  Mitigated by exiting inside
   the success arm with confirm-kill-processes nil.  Root cause
   unresolved -- flagged for T4 soak harness which will need many
   clean exits.
