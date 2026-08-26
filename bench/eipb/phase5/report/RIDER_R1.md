# EIPB Phase 5 -- Rider R1 verdict -- pure log re-read
# Executed 2026-08-26.  Contract: ../PHASE5.md section 4.
# Tool: bench/eipb/phase5/rider_r1_reread.py (offline parser,
# stdlib only; zero new runs, zero env changes, zero ENCA edits).
# Inputs: bench/results/eipb_t41.log + eipb_t42.log (+ rss csvs).

## 0. Verdict

**Outcome ② with one formal ③ sub-item: the observable GC/memory
fields are sufficient to test synchronization, and they do NOT show
it.**  B/C latency drift is not consistently synchronized with
GC-rate or memlimit state across the eight build-runs; the ratchet
steps do not locally co-move with latency jumps in ANY build;
and the strongest counter-evidence is cross-run: B degraded in
both soaks while its normalized GC rate ROSE in t41 (ratio 1.29)
and COLLAPSED in t42 (0.23).  A driver must reproduce the effect
in both runs; no GC/mem observable does.

Formal missing-field ledger (recorded, never guessed):
    gc-cons-threshold    : NEVER emitted in any EIPB4 log
                           (=> the one field that could still
                           rescue a threshold-growth story is
                           unreadable post-hoc)
    windowed type-c p95  : not emitted (p50/p99/max only)
    workload counters    : partial (ops/win only)

Routing per contract + review directive:
  - R2 (pinned-GC intervention arm) NOT TAKEN: its premise -- an
    observed GC-state/latency synchronization worth verifying --
    does not exist.
  - R3 (single-variable build bisect) is the nominal next rung for
    outcome ②, but R1 yielded NO discriminating variable to bisect
    on; a bisect without a variable is fishing (~8h rig time).
    R3 therefore requires a written variable choice BEFORE rig
    time (see section 4); meanwhile the mainline proceeds to
    P5-0/P5-1, whose W6 session-age pair collects the R4-style
    cross-check for free.
  - If nothing new lands by then, the rider PARKS unexplained --
    an accepted product under PHASE5.md section 6.

## 1. Narrow question and test design

Question: is B/C typing-latency drift synchronized with GC /
memory-pressure state changes?

Per build-run series over 180 s windows (session age known):
L_w = type-c p50; G_w = gcs_delta; N_w = ops; M_w = memlimit_kb;
Gop_w = G_w/N_w x1000 (GCs per 1000 ops -- throughput-normalized,
removes the "slower ops => fewer ops => fewer allocs" artifact);
RSS from the 60 s sidecar aligned to window midpoints.
Statistics: Spearman rho on levels and on first differences;
first-half vs last-half Gop means; local extrema around drift
onset.  Observation layer only (doctrine 8 unmet for causality).

## 2. Results (eight build-runs)

| run | build | drift class     | sp(L,mem) | sp(dL,dMem) | sp(L,Gop) | Gop half-ratio |
|-----|-------|-----------------|----------:|------------:|----------:|---------------:|
| t41 | A     | moderate drift  | +0.96     | -0.56       | -0.86     | 0.80 |
| t41 | D     | moderate drift  | +0.94     | -0.16       | -0.72     | 0.77 |
| t41 | B     | drift           | +0.81     | +0.46       | -0.14     | **1.29 (rises)** |
| t41 | C     | drift           | +0.97     | +0.05       | -0.98     | 0.53 |
| t42 | C     | strong, accel   | +0.77     | -0.01       | -0.94     | 0.87 (late collapse) |
| t42 | D     | quasi-steady    | +0.80     | -0.07       | -0.41     | 1.00 (flat) |
| t42 | A     | creep           | +0.98     | +0.02       | -0.88     | 0.72 |
| t42 | B     | worst, accel    | +0.96     | +0.11       | -0.99     | **0.23 (collapse)** |

Reading, point by point:
- sp(L, mem_MB) is high EVERYWHERE (+0.77..+0.98) including the
  steady/quasi-steady builds.  memlimit ratchets monotonically in
  all four builds (+23-24 MB each), so level correlation is a
  session-age surrogate, not a mechanism fingerprint.
- Incremental sync sp(dL, dMem) is ~zero in every t42 build
  (-0.07..+0.11): memlimit STEP events do not coincide with
  latency jumps even within-build.
- Gop half-ratio does not separate {B,C} from {A,D}: steady D sits
  at 0.77/1.00 while drifting B flips 1.29 -> 0.23 between runs.
  Same build, same direction of drift, opposite GC-rate sign =>
  GC pacing is not the stable driver of the drift.
- Pause durations independently contradict a GC-pressure story:
  windowed forced-GC p99 bounces 43-824 ms with NO trend in all
  builds (PHASE4_2 section 4) -- if collection cost were leaking
  into typing, pauses would trend too.
- Scope check: the drift is typing-specific.  Non-typing classes
  hold or SHRINK late-session (imenu/orgcycle/winedit tails < 1 TA;
  xref flat; PHASE4_2 sections 2-3).  A global memory-pressure or
  allocator-decay mechanism would slow every class; only typing
  moves.

## 3. The one genuine co-movement event (recorded, unattributed)

t42 build C, windows 31-33 (~93-99 min): gcs_delta spikes
552 -> 1750/1774 (Gop 422 -> ~1260) while median dips
157.6 -> 59.5/64.1; then Gop decays to 139-403 while the median
climbs into the 300 ms regime (w36-40).  This is the only place in
the corpus where latency and GC pacing visibly move together.
Single event, single run, single build; consistent with a heap
growth/threshold step resetting GC pacing -- but gc-cons-threshold
is MISSING-FIELD, so this stays an observation, not an attribution.

## 4. Rider state after R1

    R1  DONE      outcome ② (+③ ledger item above)
    R2  SKIPPED   premise absent (no synchronization to verify)
    R3  GATED     needs a written bisect variable before rig
                  time; candidates from build identities only:
                  p0-tree toolchain/config vs upstream (B,C share
                  the p0 lineage; A upstream; D a different fork),
                  but note D's steadiness already weakens any
                  "fork base" story -- no variable currently
                  survives scrutiny
    R4  FREE      rides on P5-1 W6 (external subject, t0 vs +2h)

Recommendation (binding unless overridden): proceed with P5-0/P5-1;
revisit R3 only if W6 shows the drift is NOT universal (i.e.,
external IDE stays flat while B/C degrade) -- that would finally
supply the discriminating frame a bisect needs.  If W6 shows the
same shape externally, the phenomenon is workload/backend-layer,
the fork chase CLOSES, and the rider ends unexplained-but-scoped.
