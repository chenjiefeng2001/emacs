# EVS-5.2 -- Completion Result Cache Prototype
# Section 5.2.0: CACHE CONTRACT (frozen 2026-08-24)

Status: approved as a **minimal Completion Result Cache prototype**
ONLY.  Semantic cache / AST cache / cross-revision reuse are NOT
approved and each requires its own contract amendment after 5.2.8.

## 0. What we cache

NOT "answers".  A cache entry is the REUSABLE COMPUTATION RESULT of
one backend completion round trip, tagged with the minimal immutable
provenance needed to decide whether it still applies:

```c
key   = { document_id, revision, cursor, trigger, prefix_hash }
value = immutable candidate model (labels + annotations)
```

v1 key discipline (Phase A): EXACT match on every field, including
revision.  Revision-relative or semantic identity reuse is Phase D
and stays unapproved until Phase C correctness is green.

The snapshot itself is never stored in the entry -- provenance fields
only, so caching cannot create retention coupling to documents.

## 1. Architecture boundary (frozen)

```text
Scheduler          <- knows: task/urgency/deadline/revision/cancel
   |                  NOT: cache/prefix/candidate/clangd/completion
   v
Completion Engine
   +---------+---------+
   |                   |
 Cache             LSP Backend
```

The cache lives in the Completion Engine layer.  A hit path NEVER
reaches the scheduler and NEVER reaches clangd (gate C11 is
structural, not statistical).

## 2. Staged implementation order (strict)

| stage | content | gate |
|---|---|---|
| A | strict-revision exact-key hit; correctness baseline | zero false hits |
| B | same-entry prefix-extension local filtering | subset oracle exact |
| C | explicit invalidation oracle across edit-type matrix | zero false hits |
| D | cross-revision / semantic reuse | separate amendment |

Prefix extension rule (Phase B): a cached entry may serve a LONGER
prefix only when document_id, revision, cursor, trigger all match and
entry.prefix is a strict PREFIX of the requested prefix.  Prefix is a
NECESSARY condition, never sufficient alone -- hence the frozen
context fields above.

## 3. Data structure (frozen for prototype)

```text
open-addressing hash on mixed key hash
+ intrusive LRU list
+ bounded: max_entries / max_bytes
```

NO ARC/LFU/admission policy/generational scheme/background eviction/
lock-free table.  Stats counters are mandatory from day one:
lookups, hits, misses, evictions, invalidations, entries, bytes.

## 4. Gates (C-series, extend EVS43 §5)

```
C8a cache_lookup p50        measured natively
C8b cache_filter p50        measured natively over model grid
C8c hit-path visible        keypress->visible on HIT workload
                            (<1ms engine-side; UI floor from EVS-4.4
                             reported separately, never hidden)
C10 false_hit == 0          property test + edit-matrix oracle
C11 backend avoidance       structural (hit path cannot reach lsp)
C12 user-path benefit       keypress->visible <1ms ON HIT workload
C13 meaningful hit rate     H1-H7 scenario rates REPORTED, threshold
                            decided by experiment, not predetermined
```

B-arms: B0 direct clangd / B1 ENCA->clangd / B2 miss->clangd /
B3 hit->local filter.  Expected shape: B0~B1~B2 ~80ms, B3 <1ms
(engine side).

## 5. Failure discipline

A wrong cached completion is worse than no cache.  Any false hit in
the C10 matrix fails the phase regardless of latency numbers, and the
prototype reverts to strict-key-only mode until root-caused.

## 7. Stage C contract -- invalidation correctness (frozen 2026-08-24)

### 7.1 Core invariant

```
cache_hit  =>  cached_result == fresh_backend_result_for_current_snapshot
```

NOT "prefix unchanged => reusable".  A miss is always permitted and
falls back to the backend; a false hit is a correctness failure that
fails the stage regardless of any latency number.

### 7.2 Entry binding (minimum provenance)

Every entry is bound to ALL of:

```text
document identity        (object id)
language/config identity (lang_hash over language_id + config epoch)
completion context/key   (cursor, trigger, prefix_hash)
validity revision        (exact ENCA revision at insert; Phase D may
                          relax this LAST, after C is green)
```

### 7.3 Invalidation policy v1 (conservative fallback)

```text
ANY document mutation that cannot be PROVEN unrelated to an entry's
context  =>  invalidate that entry.
v1 proof rule: none -- every edit invalidates the whole document.
Retain-on-unrelated is allowed only with an oracle and only after
hit-rate data justifies the complexity.
```

### 7.4 Edit-type matrix (all must MISS post-edit)

before-cursor insert / inside-prefix insert / after-cursor insert /
context modification / identifier rename / context deletion /
whitespace-only change / comment change / far-region change / undo
(revision revert) / redo / full replacement / close+reopen.

Language/config change => full-cache clear.

### 7.5 Dual-path oracle (test-phase only; production never does this)

For every candidate hit, the harness ALSO fetches the authoritative
fresh backend result for the current snapshot and compares canonical
label sets.  Test phase may pay full backend cost per case.

```
current snapshot -> cache lookup -> cached labels
                 -> fresh clangd -> fresh labels
canonical(sorted) equal  => legitimate HIT
otherwise                => FALSE HIT => stage fails
```

### 7.6 Gate additions

C12 mixed workloads (H/M patterns, revision storm) report hit_rate,
p50/p95/p99/p99.9, backend_requests_avoided, stale_drops,
false_hits.  C13 real keypress->visible stays gated on 5.2.6 (elisp
integration) and is NOT claimable from native numbers alone.

## 8. Stage 5.2.6 outcome -- real elisp integration (2026-08-24)

Cache wired into the REAL slice: enca-evs-complete runs the full user
path in a live tty Emacs (keypress -> capture/snapshot -> scheduler ->
cache-or-backend -> wakeup -> candidates -> popup overlay install ->
forced redisplay -> visible).  MISS-arm backend cost simulated at
90ms via loopback think-time injection (transport + parse are real;
server compute simulated -- clangd itself measured ~78-92ms in
EVS-4.3).

| arm | ops | hit% | keypress->visible p50 | max |
|---|---|---|---|---|
| HIT (repeat prefix) | 20 | 100% | **0.58 ms** | 2.0 ms |
| MISS (+90ms backend) | 12 | 0% | 90.4 ms | 90.6 ms |
| MIX (alternating) | 16 | 50% | 90.3 ms | 90.5 ms |

Gates:
- C8c hit-path visible <1ms: MET (p50 0.58ms incl. popup+redisplay).
- C11 backend avoidance on hit: structural (hit never leaves the
  engine) + observed zero backend traffic during HIT arm.
- C10 false hits: dual-path oracle over 7 edit cases = 0 (Stage C).
- C13 hit rate: 100% repeats / 50% alternating / 0% novel-prefix --
  reported, threshold deferred to real-typing study.

Verdict: the cache converts the ~80ms backend term into sub-millisecond
visible latency for repeat workloads -- the first two-orders-of-
magnitude user-path win in the project.  Novel-prefix misses remain
backend-bound (~90ms), which is exactly the shape modern IDEs have.

## 9. EVS-5.3 -- Real Typing Workload Closure (F1, frozen 2026-08-24)

### 9.1 Question

> In a realistic editing session, what fraction of completion requests
> can the cache safely serve without a backend round trip?

### 9.2 Stage-B completion (within-revision prefix growth) �� rule frozen

Real typing GROWS prefixes: p -> pr -> pri.  With exact-prefix keys
every keystroke would miss, making F1 meaningless.  Therefore the
Stage-B reuse rule (frozen in section 2 but not yet implemented)
lands NOW, still inside one revision:

```
HIT(extend) requires ALL of:
  same document_id AND revision AND cursor AND trigger AND lang_hash
  AND entry.prefix is a strict prefix of requested prefix
=> serve enca_ct_filter_prefix(entry.model, requested prefix)
   [server-side prefix completion guarantees the superset property]
Else MISS -> backend.
```

Selection among candidates: LONGEST entry.prefix wins.
Everything else (cross-revision, cross-cursor, semantic identity)
remains Stage D / unapproved.

### 9.3 Workload (deterministic, seeded)

Identifier typing over a vocabulary with realistic repetition:
words typed char-by-char at fixed anchors; member-access bursts;
retry; interleaved edits that bump revision and invalidate.

Vocabulary hit sources in real sessions: repeats of the SAME word at
the SAME anchor (retry/refresh), and prefix growth of the CURRENT
word.  Novel words + moved cursors = honest misses.

### 9.4 Metrics (per user-path KPI)

completion_requests / exact_hits / extend_hits / misses /
backend_requests / backend_avoided_ratio / invalidated /
keypress->visible p50/p95/p99/max per op class (hit-extend,
hit-exact, miss) / false_hits (must be 0; grow path verified against
brute-force superset oracle in native tests).

### 9.5 F2 gate (cross-revision Stage D)

F2 starts ONLY if real-typing data shows: backend_avoided_ratio
< 50% while p95 > 10ms, i.e. users still wait on the backend for a
majority of completions.  Otherwise F2 stays closed as complexity
without payoff.

## 10. F1 Outcome -- real typing workload (2026-08-24)

Raw: bench/results/evs53_real_typing.log (loopback backend,
EVS_BACKEND_DELAY_MS=90 simulated server think-time on misses).

| cell | ops | exact | extend | miss | avoided | p50 |
|---|---|---|---|---|---|---|
| identifier-growth | 32 | 9 | 2 | 21 | **34.4%** | 90.3ms(miss-dominated) |
| retry | 10 | 10 | 0 | 0 | **100%** | **0.074ms** |
| edit-interleaved | 12 | 0 | 0 | 12 | 0% | 90.4ms |

Reading:
- Retry/reselect class is PERFECT: 100% served at 74us.
- Prefix-growth typing avoids a third of backend calls via exact
  repeats + the first Stage-B extend hits.
- Edit-interleaved stays 0% by DESIGN (conservative whole-document
  invalidation); making edits retain entries is exactly what Stage D
  would need to prove, and it now has a measurable target: turning
  this 0% into >50% without a single false hit.

Stage-D gate (section 9.5) evaluation: identifier+retry classes are
already fast; the remaining backend-bound class is edit-heavy flows,
where cross-revision reuse would have to operate.  Data recorded;
Stage D remains CLOSED pending a contract amendment with an
unrelatedness proof rule.

## 11. EVS-5.3.1 -- C13/C8c FULL VERSION: real-typing classes through
##      the real popup/redisplay path (contract, frozen 2026-08-25)

### 11.1 Question

> For each REAL TYPING class (hit-exact / hit-extend / miss /
> edit-interleaved), what is the complete keypress->visible latency
> distribution (p50/p95/p99/p99.9/max) when the op ends in an actual
> popup install + forced redisplay in a live tty Emacs?

This closes the gap between section 8 (UI arms but synthetic H/M/MIX
patterns) and section 10 (real typing classes but ENGINE-side timing
only).  It is the designated input for any Stage-D decision.

### 11.2 Measurement contract

```
keypress -> enca-evs-complete (engine) -> popup overlay install
         -> redisplay'  => VISIBLE        total = whole span
```

- ONE consistent clock per op covering engine + UI + redisplay.
  (Corrects the section 8 MISS/MIX arm bookkeeping, which summed
  engine-only time for `total`.)
- Engine/UI split reported per op and summarized (install vs
  redisplay p50) so the EVS-4.4 UI floor stays visible, never hidden.
- Percentiles: nearest-rank on sorted samples, p50/p95/p99/p99.9/max.

### 11.3 Cells (deterministic, seeded; loopback backend,
###      EVS_BACKEND_DELAY_MS=90)

| cell | content | expected source mix |
|---|---|---|
| RETRY | prime once, N identical requests | hit-exact |
| GROWTH | vocabulary words typed char-by-char at one anchor (section 9.3 workload, unchanged) | exact + Stage-B extend + miss |
| NOVEL | fresh pseudo-random prefixes, revision constant | miss |
| EDITMIX | completion then revision bump each iteration | miss by design |

### 11.4 Gates

- false_hit == 0 remains ABSOLUTE (structural gates C10/C11 already
  closed in sections 7/8; this phase adds NO new cache logic, so any
  non-hit source on RETRY or any hit on EDITMIX is a harness bug).
- C8c re-affirmed PER CLASS: hit-class keypress->visible p50 < 1ms
  including popup+redisplay.
- C13 reported as full distributions per class; no threshold is
  imposed (per section 4: threshold decided by experiment).

Output lines:
`KPV31|cell|eng_ms|ui_ms|inst_ms|red_ms|total_ms|src` per op and
`SUM31|cell|ops=|exact=|extend=|miss=|avoided=%|eng_p50=|vis_p50=|vis_p95=|vis_p99=|vis_p99.9=|vis_max=|inst_p50=|red_p50=`

### 11.5 Outcome -- C13/C8c full version (2026-08-25)

Raw: bench/results/evs531_ui_typing.log (loopback backend,
EVS_BACKEND_DELAY_MS=90; buffer DISPLAYED via switch-to-buffer so
redisplay does real tty work).  Harness:
test/enca/evs531-ui-typing.el; runner: bench/enca/evs531_run.sh.

| cell | ops | exact | extend | miss | avoided | eng p50 | vis p50 | vis p95 | vis max |
|---|---|---|---|---|---|---|---|---|---|
| RETRY | 30 | 30 | 0 | 0 | 100% | **0.15ms** | 4.6ms | 13.2ms | 30.9ms |
| GROWTH | 32 | 9 | 2 | 21 | **34.4%** | 90.3ms | 94.7ms | 95.5ms | 95.5ms |
| NOVEL | 15 | 0 | 0 | 15 | 0% | 90.4ms | 95.4ms | 119.1ms | 119.1ms |
| EDITMIX | 12 | 0 | 0 | 12 | 0% (by design) | 90.4ms | 95.0ms | 98.1ms | 98.1ms |

Attribution on the hit class: install p50 ~0.04ms, redisplay p50
~4.5ms -- i.e. essentially the ENTIRE visible latency of a cache hit
is the tty redisplay floor of actually painting the popup, not the
engine and not the cache.

Cross-validation: GROWTH reproduces the F1 engine-side cell EXACTLY
(exact=9 / extend=2 / miss=21 => avoided 34.4%), confirming the UI
path did not perturb cache semantics.

Measurement correction vs section 8: the 0.58ms HIT keypress->visible
figure was taken with `redisplay' running while the work buffer was
NOT displayed (no switch-to-buffer), so redisplay had almost nothing
to paint.  With the buffer genuinely displayed the honest hit-class
keypress->visible is ~4.6ms p50, dominated by the ~4.5ms tty
redisplay floor.  Engine-side remains <1ms (gate C8c engine clause
MET); the UI floor is reported separately per EVS-4.4 discipline.
Section 8 numbers stand as measured but must be quoted with this
caveat.

Gates:
- C10/C11: unchanged structural closures; no anomalies (RETRY all
  hit, EDITMIX all miss by design).
- C8c: engine-side <1ms MET per class; full-path hit p50 4.6ms with
  attribution above.
- C13: full distributions reported for all four classes.

Stage-D input (unchanged): the only backend-bound classes remain
NOVEL (honest cold misses, modern-IDE shape) and EDITMIX (0% by
conservative invalidation).  Turning edit-interleaved reuse from 0%
to >50% at false_hit=0 is still the single real lever; everything
else is at or below the tty paint floor.
