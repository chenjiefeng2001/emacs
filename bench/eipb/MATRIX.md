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

## Tier 3 user-path coverage (Phase 3, filled 2026-08-25)

Source: bench/results/eipb_t3.log (12 sessions = A/B/C/D x 3,
shuffled orders); verdicts in bench/eipb/phase3/report/PHASE3.md.
Pooled means; observed differences only (doctrine 8).

| cell                        | headline (all builds)                  |
|-----------------------------|----------------------------------------|
| isearch steady key p50      | 0.11-0.16 ms @100KB AND @1MB           |
| isearch first key (BOB scan)| 1.6-2.8 ms @100KB; 7-16 ms @1MB        |
| open io segment             | ~4 ms/MB warm, linear, same band x4    |
| open idle drain (sit-for 0) | 3-15 MICROseconds                      |
| elisp cold fontify on open  | 100KB ~0.4-1.0 s; 1MB ~5.5-6.5 s       |
| gcs_delta protocol check    | EXACT match across builds (117/117/117/117) |

New stalls found: NONE at <=10MB warm scale -- file I/O and isearch
join the cleared column.  Proven interactive stalls remain: c-mode
jit chunks >=100ms (T2), large-heap GC pauses 190-320ms (T1),
multi-window repaint multiplication (~45-47ms @8 panes, T2 Q3).
Pending: cold-cache opens, IDE-MIXED-01, xref/imenu, org/dired,
GUI variants.

## Tier 3.1 IDE-MIXED-01 (2026-08-25)

Source: bench/results/eipb_t31.log (8 sessions = A/B/C/D x 2,
shuffled orders); verdicts in bench/eipb/phase3/report/PHASE3_1.md.
Composition validated: session cost = sum of parts; no emergent
interaction stall.  New coverage entry:

| action                    | p50 ms (A/B/C/D)            |
|---------------------------|-----------------------------|
| completion-at-point round | 68 / 46 / 55 / 58 (~50ms typical, incl. Completions render) |
| typevis c-mode mid-burst  | 17 / 12 / 11 / 10           |
| undo step mid-session     | 0.21 flat                   |

Tails stay thin-n; ENCA shows no direction on any action.

## Tier 3.2 xref/imenu + org (2026-08-26)

Source: bench/results/eipb_t32.log (8 sessions = A/B/C/D x 2,
shuffled); verdicts in bench/eipb/phase3/report/PHASE3_2.md.
pooled p50 ms (format A/B/C/D):

| cell                | p50 (A/B/C/D)            | class     |
|---------------------|--------------------------|-----------|
| imenu cold index    | 0.27/0.25/0.24/0.26 cached; first-build ~300-560 one-shot | **stall** |
| im/goto             | 0.21-0.42 all            | closed    |
| xr/scan (grep)      | 67/90/83/98              | mapped    |
| org/cycle subtree   | 4.7/4.3/4.5/5.0          | closed    |
| org/global sweep    | 500/423/499/561 (p95 to 1197) | **stall** |
| org/nav             | 0.8 flat                 | closed    |
| org cold fontify    | 1744/2740/3161/1897 (1-shot, n=2) | mapped |

Protocol check: xr matches_mean = 1068.0000 in every build.
Atlas after 3.2 -- stalls: c-jit >=100ms; GC 190-320ms;
multi-window ~45ms; imenu cold ~300-560ms; org/global ~0.5s.
Pending: dired, cold-cache opens, GUI, soak (T4).

## Tier 3.3 dired + T3 CLOSURE (2026-08-26)

Source: bench/results/eipb_t33.log (8 sessions = A/B/C/D x 2,
shuffled); verdicts in bench/eipb/phase3/report/PHASE3_3.md.
pooled p50 ms (A/B/C/D):

| cell             | p50 (A/B/C/D)          | class  |
|------------------|------------------------|--------|
| dd/open-big(2000)| 234/177/222/226        | mapped |
| dd/open-small    | ~10 flat               | closed |
| dd/refresh       | 7.1/7.3/7.5/8.4        | closed |
| dd/create-rename-delete | ~8-9 all builds | closed |
| dd/jump          | ~2.3 flat              | closed |

**A ~= B ~= C ~= D everywhere: no ENCA signal in dired.**

===============================================================
EIPB LATENCY ATLAS -- FROZEN v1 (T3 closure, 2026-08-26)

CLOSED: buffer edit / undo / isearch incremental / file open chain /
LSP transport / completion transport / IDE mixed composition /
xref scan / capf round / dired ops / interactive org.

MAPPED: capf round ~45-70ms; xref scan ~70-100ms; dired big-dir
~0.2s one-shot; org cold fontify ~1.7-3.2s @300KB.

STALLS (Emacs-core properties, ENCA-independent):
  c-mode jit chunks        >=100 ms
  large-heap GC pause      190-320 ms
  multi-window repaint     ~45-47 ms @8 panes
  imenu cold index         ~300-560 ms one-shot
  org global sweep         ~0.5 s @2000 headings

DEFERRED BY DECISION: cold-cache opens / GUI variants / magit flows.
Phase 4 gate: soak must prove closed rows stay closed and stall rows
stay stable over 30min/2h (slope-based RSS, tail amplification ratio,
kill-emacs exit hang attribution REQUIRED).
===============================================================

## Tier 4.1 SOAK-30M (2026-08-26)

Source: bench/results/eipb_t41.log (+ eipb_t41_rss.csv); verdicts in
bench/eipb/phase4/report/PHASE4_1.md.  One continuous 30-min mixed
session per build, shuffled order A-D-B-C, ~13.7k ops each.

| gate                        | A    | D    | B    | C     |
|-----------------------------|------|------|------|-------|
| ta/overall (<=2.0)          | 1.57 | 1.43 | 1.19 | 2.15* |
| RSS slope post-warmup KB/h  | 201  | 104  | 165  | 111   |
| teardown / kill-emacs exit  | clean| clean| clean| clean |

(*) FAIL-as-measured then UNRESOLVABLE per doctrine 7: all builds
converge to the same absolute tail p99 band 451-642 ms; C's low
head baseline (260 vs 341-404 ms) inflates the ratio.  No ENCA
direction (disabled-B tracks enabled-C/D).

Atlas addendum v1.1: CLOSED rows stayed closed under sustained load
(imenu/orgcycle/dired/winedit tails SHRINK after warmup; xref flat).
Stall rows bounded, no takeoff (forced-GC p99 bounces 93-751 ms,
no trend).  NEW build-independent finding: insert-typing medians
grow ~4x over 30 min in ALL builds incl. disabled-B (type-c p50
16->70..91 ms) -- saturate-vs-unbounded handed to SOAK-2H as the
phase's open question.

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
