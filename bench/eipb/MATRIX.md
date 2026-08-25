# EIPB Performance Matrix -- Tier 1 Core Interactive
# Filled 2026-08-25 from bench/results/eipb_t1.log (tty, WSL,
# single session per build; repeat before regression conclusions).
#
# Builds:
#   A = upstream vanilla          (~/enca-p0-a)
#   B = fork, ENCA disabled       (~/enca-p0-b)
#   C = fork, ENCA enabled @P0    (~/enca-p0-c)
#   D = fork, current full ENCA   (~/enca-p11)

## Latency cells (p50 ms unless noted)

| cell                                   |      A |      B |      C |      D |
|----------------------------------------|-------:|-------:|-------:|-------:|
| startup batch (steady run3, wall)      | 284    |  82    | 172    | 220    |
| startup tty cold (wall)                | 157    | 590*   | 209    | 208    |
| startup tty warm (wall)                | 125    | 479*   | 272    | 259    |
| edit/64KB ins-eob-1B                   | 0.0010 | 0.0007 | 0.0010 | 0.0010 |
| edit/64KB ins-mid-1B                   | 0.0012 | 0.0012 | 0.0012 | 0.0012 |
| edit/1MB ins-eob-1B                    | 0.0012 | 0.0079 | 0.0005 | 0.0010 |
| edit/1MB ins-mid-1B                    | 0.0012 | 0.0014 | 0.0012 | 0.0014 |
| edit/1MB del-mid-1B                    | 0.0010 | 0.0007 | 0.0007 | 0.0005 |
| edit/1MB paste-1KB                     | 0.0021 | 0.0031 | 0.0026 | 0.0033 |
| edit/10MB ins-mid-1B                   | 0.0010 | 0.0007 | 0.0010 | 0.0083 |
| edit/10MB del-mid-1B                   | 0.0007 | 0.0007 | 0.0010 | 0.0010 |
| edit/1MB ins-eob-1B + visible          | 1.13   | 0.60   | 1.10   | 0.71   |
| rdisp/R0-noop                          | 0.051  | 0.054  | 0.053  | 0.064  |
| rdisp/R1-one-change (+repaint)         | 1.38   | 1.38   | 1.44   | 1.77   |
| rdisp/R2-popup                         | 0.87   | 1.37   | 1.44   | 2.07   |
| rdisp/R3-100-changes (+repaint)        | 3.14   | 5.87   | 4.11   | 2.89   |
| gc forced-pause                        | 29.2   | 28.8   | 18.3   | 29.3   |
| undo single-keystroke                  | 0.0014 | 0.0012 | 0.0012 | 0.0012 |
| search/1MB literal sweep               | 105.5  | 91.2   | 108.0  | 73.5   |
| search/1MB regexp-id                   | 36.5   | 107.7  | 61.7   | 127.3  |
| search/10MB literal sweep              | 1667   | 2052   | 1676   | 3225   |
| search/10MB regexp-id                  | 530.9  | 775.6  | 664.0  | 969.3  |

## Tail metrics (max / p99 ms)

| cell                              | A            | B           | C           | D           |
|-----------------------------------|-------------:|------------:|------------:|------------:|
| gc forced-pause p99               | 239.4        | 208.8       | 166.8       | 244.6       |
| gc forced-pause max               | 270.0        | 225.7       | 189.1       | 275.6       |
| gc/storm mean pause (160MB live)  | 346.9        | 254.6       | 287.1       | 247.5       |
| edit/1MB visible p99              | 9.0          | 3.6         | 37.8*       | 6.7         |
| rdisp/R3 max                      | 14.9         | 14.7        | 14.1        | 7.4         |

## Tier 2 attribution (Phase 2, filled 2026-08-25)

Source: bench/results/eipb_t2.log (467 lines/build, D+A, single
session each); verdicts in bench/eipb/phase2/report/PHASE2.md.
p50 ms unless noted; format D / A.

| cell                                  | D           | A           |
|---------------------------------------|-------------|-------------|
| fl/c/100KB natural p99                | 112         | 100         |
| fl/c/100KB natural stalls>100ms (/40) | 6           | 1           |
| fl/org/1MB natural p50                | 12.7        | 3.7         |
| fl/elisp/1MB natural p50              | 0.78        | 0.67        |
| fl/python/1MB natural p50             | 3.20        | 4.25        |
| fl plain control natural p50 (1MB)    | 20.4        | 12.3        |
| full-fontify cold c/1MB (one-shot s)  | 108         | 110         |
| gcp/nogc max (EOB typing 1000 ops)    | 31.0        | 31.6        |
| gcp/natural max                       | 38.0        | 36.1        |
| gcp/forced max                        | 52.1        | 51.0        |
| win1 -> win8 editvis p50              | 19.9 -> 47.2| 11.7 -> 26.3|
| win8 noop p50                         | 0.13        | 0.13        |
| buf100 switch p50                     | 47.3        | 17.7*       |
| spot8x100 editvis p50                 | 48.4        | 12.6*       |
| memory end (KB)                       | 42404       | 40356       |

## Pending (future tiers)

file I/O ladder / isearch interactive / IDE-MIXED-01 trace /
SOAK-30M+2H / GUI variants / external IDE reference:
**PENDING (EIPB.md T3-T5)**.
Multi-session tail comparison across builds: PENDING (doctrine 3).

## Tier 2.1 attribution closure (2026-08-25)

The mid-buffer D-vs-A gap flagged above was re-measured under
interleaved rounds A,B,C,D x2 with GC pinned and deterministic
traces: it did NOT replicate (D/A medians 0.66-1.17, B~C~D on every
cell, only vanilla-A off-band in the slow direction = run-order
artifact).  ENCA cleared; no fork-base effect.  Details:
bench/eipb/phase2/report/PHASE2_1.md (REPORT section 32).

## Notes

- * = noise flag. Startup walls at the 100-300ms scale moved >2x
  between builds/runs (system noise); treat startup rows as
  provisional until N>=5 runs with median reporting (Phase 2).
- Search sweeps time an elisp while-loop around search-forward /
  re-search-forward: they include per-match Lisp call overhead
  (~600k matches on 10MB literal) and are NOT a pure C scan metric.
- The dominant interactive-stall discovery of T1 is GC: forced full
  GC pauses of p50 ~18-30ms and max ~190-320ms exist IDENTICALLY IN
  VANILLA.  This is an Emacs-core property, not an ENCA effect, and
  it dwarfs every other tail in the table.
- ENCA impact across editing/redisplay/search/undo: differences sit
  inside the noise band; no cell shows a systematic C/D regression.
- Tier 2 * = unreplicated single-session gap.  RESOLVED 2026-08-25
  by Phase 2.1 (interleaved B/C/D re-measure): artifact of fixed run
  order, not ENCA -- see the Tier 2.1 section below.
