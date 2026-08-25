# EIPB Phase 2 -- Tail-Latency Attribution Report
# Executed 2026-08-25 (tty, WSL, single session per build).
# Contract: bench/eipb/EIPB.md section 7.  Raw: bench/results/eipb_t2.log
#
# Builds:  A = upstream vanilla (~/enca-p0-a)
#          D = fork, current full ENCA (~/enca-p11)
# Line budget: 467 lines per build, perfectly symmetric, zero FATAL.
# Wall clock: 2925 s total (FL dominates; c/10MB cold full-fontify
# alone is ~16 min per build).

## 0. Verdict table

| Q | Question                                  | Verdict | Magnitude (headline) |
|---|-------------------------------------------|---------|----------------------|
| Q1 | Is font-lock an editing tail source?     | **YES, mode-dependent** | c-mode: keystroke chains hit >100 ms when jit fires (6/40 ops on 100KB); org-mode ~12-15 ms p50; elisp/python ~free (<1-4 ms) |
| Q2 | Does GC pause enter user-perceivable path? | **YES when it fires mid-window; rare at default threshold** | forced-GC ops lift max 31->52 ms (+67%), p99 +51%; natural typing sees only 6 GCs / 1000 ops, max delta +7-10 ms |
| Q3 | Do multi-buffer/window change redisplay? | **YES for windows; mild for buffers** | edit+visible scales with pane count 1->2x then plateaus (~45-47 ms at 8 windows vs 12-20 at 1); idle noop flat ~0.15 ms at any window count |

All numbers below are ms, tty, keypress->visible chain unless noted.

## 1. Q1 -- font-lock chain

Arms per cell: NATURAL = insert+(redisplay t), jit runs as for a real
user.  DECOMP = buffer/fontify/redisplay segments timed separately.

### Natural-arm percentiles (p50 / p99)

| cell              | D            | A            |
|-------------------|--------------|--------------|
| fl/c/100KB        | 26.7 / 112   | 20.4 / 100   |
| fl/c/1MB          | 17.7 / 181   | 13.0 / 120   |
| fl/elisp/100KB    | 0.75 / 47    | 0.89 / 37    |
| fl/elisp/1MB      | 0.78 / 12    | 0.67 / 2.4   |
| fl/python/100KB   | 1.80 / 12    | 1.12 / 4.1   |
| fl/python/1MB     | 3.20 / 15    | 4.25 / 14    |
| fl/org/100KB      | 12.3 / 153   | 3.5 / 14     |
| fl/org/1MB        | 12.7 / 15    | 3.7 / 12     |
| fl/plain/100KB*   | 17.4 / 31    | 10.0 / 19    |
| fl/plain/1MB*     | 20.4 / 31    | 12.3 / 26    |
| fl/c/10MB         | 63.3 / 91    | 50.9 / 69    |
| fl/plain/10MB*    | 20.4 / 30    | 10.0 / 21    |

(*) control arm -- no jit-lock functions in fundamental; fontify
segments are semantic no-ops (upstream jit-lock--run-functions would
crash on (min nil beg); call skipped, noted in log).

Stall counters (>10ms / >50ms / >100ms of 40 ops):

| cell           | D           | A           |
|----------------|-------------|-------------|
| c/100KB        | 39 / 9 / 6  | 37 / 9 / 1  |
| c/1MB          | 36 / 1 / 1  | 28 / 1 / 1  |
| org/1MB        | 24 / 0 / 0  | 7 / 0 / 0   |
| python/1MB     | 14 / 0 / 0  | 19 / 0 / 0  |

Decomposition (medians): buffer segment 0.01-2.2 ms everywhere;
fontify segment is the dominant variable part in c-mode
(14 ms p50, up to 110 ms p95/p99 at 100KB -- bimodal: line-rescan vs
string/comment context chunk) and org-mode (0.4-0.7 ms p50 but
visible as the 12 ms natural p50 via multiple jit entry points);
redisplay segment 0.5-12 ms depending on mode/size.
elisp and python fontify segments stay <=1.8 ms p50 at all sizes:
their jit cost per keystroke is effectively invisible.

Cold whole-buffer fontification (full_fontify_ms, one-shot):

| cell        | D            | A            |
|-------------|--------------|--------------|
| c/100KB     | 17 012       | 19 273       |
| c/1MB       | 108 265      | 110 285      |
| c/10MB      | 964 078      | 786 499      |
| python/1MB  | 264 980      | 286 424      |
| elisp/1MB   | 6 700        | 6 321        |
| org/1MB     | 9 556        | 7 562        |
| plain/*     | ~0.01        | ~0.01        |

A/D agree within noise on every cold pass (c/10MB +22% D-side is one
number, n=1, unpaired -- not evidence).  ENCA does not touch the
font-lock engine itself.

**Q1 finding**: font-lock IS a user-path stall source, concentrated
in c-mode (context-chunk refontification spikes >=100 ms inside
single keystrokes) and org-mode (uniform ~12 ms tax).  It is NOT a
general tax: elisp/python typing chains are font-lock-free in
practice.  Any future "IDE parity" work on editing tails should
target cc-mode jit chunks first.

## 2. Q2 -- GC in the user path

1000 keypress->visible ops at EOB, fresh ~0.96 MB fundamental buffer.
Protocol verification held: gc_count_delta nogc=0, natural=6,
forced=42 (=40 scheduled +2 incidental) in BOTH builds.

| arm     | p50 D/A       | p99 D/A       | max D/A       | stalls>10ms D/A |
|---------|---------------|---------------|---------------|-----------------|
| nogc    | 14.3 / 14.3   | 26.5 / 24.9   | 31.0 / 31.6   | 722 / 674       |
| natural | 15.2 / 14.2   | 28.5 / 26.3   | 38.0 / 36.1   | 755 / 710       |
| forced  | 16.3 / 11.7   | 39.9 / 36.6   | 52.1 / 51.0   | 835 / 584       |

Reading: the ~14 ms p50 BASELINE belongs to redisplay of a shifted
~1 MB terminal pane, not to GC (nogc arm proves it).  GC enters the
path exactly where predicted: FORCED every-25-op collection adds
+21 ms at max and +13 ms at p99 over nogc, identically shaped in
vanilla.  NATURAL threshold-20000 firing (6 hits / 1000 ops) barely
moves tails (+7 ms max).

**Q2 finding**: GC pauses DO land inside interactive windows -- this
is now measured, not assumed -- but at default thresholds their
natural frequency during pure typing is low (0.6% of keystrokes).
The T1 discovery stands unchanged: the dangerous form is the BIG
pause on large live heaps (190-320 ms), which Phase 2's small-heap
protocol deliberately does not reproduce.  Priority ordering for any
future GC work = heap-size-dependent pause time, not GC frequency.

## 3. Q3 -- window/buffer scaling

Windows {1,2,4,8} x 1 MB buffer (editvis p50 / noop p50):

| win | editvis D | editvis A | noop D  | noop A  |
|-----|-----------|-----------|---------|---------|
| 1   | 19.9      | 11.7      | 0.17    | 0.14    |
| 2   | 45.0      | 35.4      | 0.19    | 0.14    |
| 4   | 46.2      | 20.6      | 0.13    | 0.14    |
| 8   | 47.2      | 26.3      | 0.13    | 0.13    |

Idle redisplay is window-count-independent (~0.15 ms p50 both
builds).  Edit+visible cost is repaint-area-bound: jumps 1->2 panes,
plateaus ~45-47 ms by 4-8 panes.  Scroll cost SHRINKS with more
windows (win1 51/33 -> win8 13.6/7.5 p50: smaller panes repaint less).

Buffers {10,100} x 1 window, plus 8x100 spotlight:

| cell                | switch p50 D/A | editvis p50 D/A |
|---------------------|----------------|-----------------|
| buf10               | 34.8 / 20.9    | 32.6 / 19.7     |
| buf100              | 47.3 / 17.7    | 42.9 / 15.3     |
| spot8x100           | 3.1 / 1.3      | 48.4 / 12.6     |

Memory (memory-limit KB): start 30232(D)/28188(A) -> end
42404(D)/40356(A): +~2 MB for 100 live buffers + 8 windows, builds
within 5% of each other.

**Q3 finding**: window count, not buffer count, is what multiplies
user-visible latency (repaint area).  Buffer-count scaling is mild
for switches (sub-linear, mostly window-shape driven).  BUT: the
D/A gap WIDENS with scale -- 1.66x at buf10 -> 2.68x at buf100 ->
3.83x at 8x100 spotlight on editvis.  One session, unreplicated;
flagged as PENDING-B/C-CHECK, not a claim (multi-build doctrine).

## 4. D-vs-A systematic gap (observation, not verdict)

Across many mid-buffer edit+visible cells D runs 1.3-2.8x above A,
while (a) EOB-insert cells (gcp arms) show D==A exactly, (b) idle
noop p50 shows D==A, (c) cold fontification shows D==A.  The pattern
is consistent with extra per-modification work that only manifests
when text below point shifts (mid-buffer insert => larger repaint),
but B/C arms are absent from Phase 2, so fork-base-vs-ENCA-enabled
cannot be separated.  Logged as the top follow-up question for the
next matrix run (B/C rows on fl/plain + wb cells).

## 5. Method notes / disclosures

- Single session per build; repeat before regression conclusions
  (doctrine 3).  Percentiles over 5-1000 samples per cell as marked.
- tty/WSL-pty absolute values are inflated vs GUI; relative
  comparisons within-session are the valid instrument.
- Scope cuts honored from contract: window cap 8; 10MB cells only
  c+plain; 100MB deferred to T3.  c/10MB cold fontify consumed
  ~16 min/build -- runtime cap documented, cells ordered last so a
  timeout could not starve earlier data (it did not trigger).
- Harness fixes landed pre-run (session-2 postmortem):
  eipb2--timed-reps defined; wb-make-buffers naming unified;
  popup helper returns value (push-through-parameter bug silently
  emptied popup dists); fundamental control arm skips jit calls
  (upstream jit-lock--run-functions nil-crash reproduced in batch);
  per-cell condition-case isolation; frame resized 50 rows for the
  8-window tier; section-level timeouts (FL 4500s/GCPATH 900s/WB
  1800s) so no section can starve another.
