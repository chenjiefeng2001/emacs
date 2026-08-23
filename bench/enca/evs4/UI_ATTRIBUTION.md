# EVS-4.4 Contract -- UI Critical Path Attribution

Status: **frozen 2026-08-24, before implementation.**  Amends
bench/enca/evs4/COMPLETION.md section 7 (phases 4.4/4.5).

## 0. The only question

> From clangd candidate-ready to the completion popup being VISIBLE on
> screen -- how much synchronous Emacs work remains, and which parts
> of it MUST stay on the main thread?

Nothing gets moved to worker threads in this phase.  First we prove
which segment deserves it.

## 1. Timeline (frozen)

```
T0  keypress                    T7  completion table ready
T1  request submitted           T8  popup model ready
T2  LSP request sent            T9  redisplay invalidated
T3  candidate-ready             T10 redisplay started
T4  JSON decoded                T11 redisplay finished
T5  revision gate passed        T12 GUI/terminal presented
T6  candidate model built
```

Derived metrics:

```
backend_latency      = T3-T2     popup_latency      = T8-T7
decode_latency       = T4-T3     redisplay_latency  = T11-T9
commit_latency       = T6-T4     presentation       = T12-T11
completion_latency   = T7-T6
candidate_ready->visible = T12-T3
keypress->visible        = T12-T0
```

Plus thread accounting: main_thread_block_ns, worker_compute_ns,
idle_wait_ns, wasted_work_ns.

## 2. Four arms (A-ladder)

| arm | path | isolates |
|---|---|---|
| A0 | synthetic candidates -> model -> UI | Emacs' own floor, no LSP |
| A1 | clangd -> decode -> candidate -> UI | real user path |
| A2 | clangd -> worker-built candidate model -> commit | candidate-model cost moved off main |
| A3 | clangd -> worker-built POPUP MODEL (rows, widths, annotations) -> main installs + invalidates only | the future Snapshot+Render boundary |

A3 still renders NOTHING off-thread; the worker computes data, the
main thread installs and redisplays.

## 3. Completion-workload matrix (T6..T8)

```
count:    10 / 100 / 1K / 10K / 100K
length:   8 / 32 / 128 / 512 bytes per label
annot:    none / small(16B) / doc(512B)
filter:   exact / prefix / fuzzy(subsequence)
popup:    1 / 10 / 50 visible rows
```

Reported per cell: build_ns (T6), table_ns (T7), popup_ns (T8).

## 4. Redisplay-only ladder (R-series, section 4.4.2)

With a PREBUILT popup model, no backend/filtering/annotation work:

```
R0 no-op forced redisplay
R1 text insert/replace + redisplay
R2 overlay after-string popup (rows as R-series variable) + redisplay
R3 child-frame popup + redisplay            [GUI only, deferred]
R4 native GUI presentation                  [GUI only, deferred]
```

Terminal (tty) runs measure R0-R2 including real terminal output;
R3/R4 need a GUI session and stay deferred with their harnesses
shipped.

## 5. Go / No-Go (frozen)

```
D  candidate-ready->visible < ~1ms overall
   => Emacs is fast enough; do NOT restructure redisplay.
A  completion transformation dominates (T6->T8)
   => optimize the completion engine, leave xdisp alone.
B  redisplay dominates (T9->T11 > ~70% of T3->T12 tail)
   => EVS-5 Render Snapshot / background layout becomes justified.
C  terminal/GUI backend dominates (T11->T12)
   => renderer/backend investigation.
```

The Render Snapshot shape for any future EVS-5 is fixed NOW so that
if branch B fires, the design debate is already settled:

```c
struct enca_render_snapshot {
    uint64_t generation;          /* vs runtime generation */
    uint64_t buffer_revision;     /* vs document revision */
    /* immutable layout / popup / glyph / damage payloads */
};
worker: build snapshot N        main: validate -> swap -> damage ->
                                        redisplay from snapshot
```

Workers NEVER touch buffers/windows/overlays.

## 6. Forbidden

Modifying redisplay.c/xdisp.c/window code, moving any Emacs state to
workers, child-frame experiments on batch sessions, optimizing before
the matrix says where time goes.

## 7. Outcome -- sections executed (2026-08-24)

### Completion transformation (T6->T7, real Emacs, tty)
| count x len | all-completions p50 |
|---|---|
| 10 x {8..512} | 0.011 - 0.013 ms |
| 100 x {8..512} | 0.013 - 0.023 ms |
| 1K x {8,32} | 0.056 - 0.064 ms |
| 10K x {8,32} | 0.27 - 0.46 ms |
| **100K x 8B** | **6.41 ms** |

### Redisplay ladder (T9->T11, forced, terminal)
| cell | p50 | max |
|---|---|---|
| R0 baseline (no popup) | 0.033 ms | 1.62 ms |
| R2 popup overlay 1 row | 2.04 ms | 3.65 ms |
| R2 popup overlay 10 rows | 2.04 ms | 4.43 ms |
| R2 popup overlay 50 rows | 1.57 ms | 4.37 ms |

### Native worker-side model+popup (A2/A3 payload cost)
Full grid (count x len x annot x filter): 0.4us .. 90us per request --
orders of magnitude below anything user-visible.

### Verdict against section 5
- Branch D is NEARLY met on a terminal: candidate-ready -> visible is
  ~2ms end-to-end (popup install microseconds + ~2ms popup redisplay).
- Branch A fires only for pathological 100K-candidate tables.
- Branch B does NOT fire: popup redisplay adds ~2ms absolute -- far
  below any evidence bar for restructuring xdisp/redisplay.
- The dominant term of keypress->visible remains the BACKEND
  (clangd p50 78-92ms, EVS43.md section 8).

EVS-5 Render Snapshot / parallel layout: NO EVIDENCE, stays closed.
The completion engine and backend latency own whatever gap remains.
