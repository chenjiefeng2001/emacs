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
