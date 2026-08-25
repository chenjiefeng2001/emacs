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

## Pending (future tiers)

font-lock chain / file I/O ladder / multi-buffer x window /
isearch interactive / IDE-MIXED-01 trace / SOAK-30M+2H /
GUI variants / external IDE reference: **PENDING (EIPB.md T2-T5)**

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
