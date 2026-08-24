# EVS-5 -- Real Completion / Semantic Latency
# Section 5.0: Performance Contract (frozen 2026-08-24, no code yet)

Status: **contract frozen before any implementation.**  Per the
ENCA Performance Freeze (../src/enca/ARCHITECTURE.md section 26) the
project is re-scoped from "Next-Gen Emacs Core" to:

> **ENCA Real Completion / Semantic Latency** --
> make completion latency approach IDE-grade by attacking the only
> dominant term: backend round-trips and their context.

## 0. Baseline (measured, REPORT.md sections 18-22)

```
keypress -> visible  ~= 80 ms, decomposed as:

  ENCA runtime        < 0.1 ms   frozen, closed
  completion UI       ~2 ms      tty redisplay incl.
  LSP backend clangd  78-92 ms   <-- THE term
```

## 1. Research directions (exactly three)

### D1 Backend Context Engineering
What do we actually SEND clangd per request, and does it matter?

Measure as first-class metrics:
`context_bytes`, `open_docs`, `diagnostics_pending`, `index_state`,
`position_bucket`, plus their correlation with T2->T3.

Hypothesis: a meaningful share of the ~78ms is server-side work we
trigger by how much state the session carries -- not intrinsic
analysis cost.

### D2 Cancellation / Speculation
Extend P3's proven drop-before-compute INTO the backend.  Four
explicit layers, each counted separately:

```
queued cancellation      admission supersession (exists)
in-flight cooperative    worker abort hook       (P1 cancel exists)
backend cancellation     $/cancelRequest         (transport exists)
result stale-drop        revision gate           (exists)
```

Metrics: `backend_waste_ratio`, `cancellation_latency`,
`storm_final_candidate_ms`.

### D3 Completion Cache / Speculation
Modern IDEs do not recompute every keystroke.  Client-side cache over
previous responses:

```
prefix "pri" -> response R (print, printf, println, private, ...)
next "prin"  -> serve by SUBSET-FILTERING R   (no round trip)
invalidation -> any didChange that touches the cached region
```

Target shape: `cache hit < 1ms; miss ~80ms; warm steady-state <= 15ms`.

## 2. Frozen experiment matrix

| id | setup | question | primary metric | decision rule |
|----|-------|----------|----------------|---------------|
| C1 | cold session + didOpen + FIRST completion | unavoidable cold cost | T2->T3 cold | sizes the cache prize |
| C2 | warm steady-state single keystroke | true per-keystroke cost | T2->T3 p50/p95 | if p95 <= 15ms, D3 deprioritized |
| C3 | repeated prefix narrowing pri->prin->... served client-side | can filtering replace round trips? | served-latency + subset-correctness | filter <1ms AND superset holds => ship cache |
| C4 | typing storm f/fo/foo... at {1,5,10,20,50}ms vs real backend | wasted backend work | backend_waste_ratio | >30% waste => D2 ships first |
| C5 | same doc, cursor swept across offsets | position-dependence of latency | T2->T3 by bucket | strong dependence => D1 lever confirmed |
| C6 | didChange(version=k)+completion pairs | sync-keep-current cost | delta vs C2 static | large delta => debounce/batch design |
| C7 | request then mid-flight $/cancelRequest | does Layer-2 cancel reduce backend work? | post-cancel responses, waste | effective => wire into scheduler supersession |
| C8 | identical repeated request (no edits) | pure cache-hit ceiling | served p50 | target <1ms |
| C9 | unseen prefix right after invalidating edit | miss penalty incl. invalidation | p50 vs C2 | invalidation must stay <1ms |
| C10 | workspace with N=10 files open | project/index state effect | T2->T3 vs single-file | informs workspace strategy |

All experiments reuse the T0-T12 timeline from UI_ATTRIBUTION.md and
the B0/B1/B2 arm discipline from EVS43.md.

## 3. Cache architecture sketch (data contract only -- NO code in 5.0)

```c
struct enca_ct_cache_entry {
    /* key */
    enca_object_id document;
    uint64_t base_revision;      /* revision the response belongs to */
    char     prefix[64];
    /* payload */
    struct enca_ct_candidate *items;   /* immutable, refcounted like pieces */
    size_t   count;
    uint64_t expires_at_ns;            /* superseded by next didChange */
};
/* hit path: prefix is a strict EXTENSION of entry.prefix
             AND no didChange touched [cursor_start, cursor_end)
          => subset-filter items locally (<1ms), never ask backend. */
/* invalidation: any edit intersecting the cached context range OR
   version bump drops the entry into pending_reclaim-style cleanup. */
```

Ownership rules inherit SNAPSHOT.md L1-L6 discipline; cache entries
are immutable and refcounted, never mutated in place.

## 4. Phases and gates

| Phase | Content | Gate |
|---|---|---|
| 5.0 | this contract | frozen |
| 5.1 | attribution harness for C1/C2/C5/C6 (reuses lsp test binary) | baseline table produced |
| 5.2 | D3 cache prototype behind existing completion API | C8 <1ms && C9 invalidation <1ms && correctness oracle green |
| 5.3 | D2 cancellation wiring into executor path | C7 shows measurable waste reduction |
| 5.4 | D1 context strategy experiments | context_bytes/latency curve published |
| 5.5 | closure record | written verdict: which tier profile achieved |

## 5. Stop conditions

- If C2 warm p95 lands <=15ms WITHOUT cache: D3 demoted, D1/D2 become
  primary (caching complexity not bought by evidence).
- If cancellation shows <10% waste reduction: keep Layer-1 gate only,
  drop $/cancelRequest wiring.
- If context_bytes shows zero correlation with latency: D1 closes.
- Everything in ARCHITECTURE.md section 26 remains closed regardless
  of EVS-5 outcomes.

## 6. Outcome -- sections 5.1-5.3 executed (2026-08-24, clangd 19.1.0)

Raw stream: bench/results/evs5_backend_attribution.log

### 5.1 Context sweep (D1)
| pre-cursor context | p50 | p95 |
|---|---|---|
| 32B | 77.3ms | 92.4ms |
| 256B | 90.1ms | 107.3ms |
| 1KB | 92.5ms | 108.3ms |
| 4KB | 77.8ms | 92.2ms |
| 16KB | 80.5ms | 95.8ms |
| 32KB | 92.1ms | 109.5ms |

NO INFLECTION POINT across 1000x context growth: latency lives in a
flat 77-92ms band = clangd's intrinsic per-request pipeline.
**D1 (context minimization) is FALSIFIED as a lever.**

### 5.2 Stale backend work (D2)
Pipelined 12-request storms, arrivals observed:

```
burst, no cancel      responses 7/12, drain 0.41ms
burst + $/cancel      responses 3/12, drain 0.50ms
10ms cadence + cancel responses 1/12, drain 172ms (one full compute)
50ms cadence + cancel responses 2/12, gaps ~0.12ms
```

clangd ALREADY supersedes/coalesces queued identical completion
requests server-side: superseded requests are answered near-
instantly (~0.01ms gaps) or not at all, and only the newest request
pays the full pipeline.  ENCA's drop-before-compute and clangd's
internal supersession are COMPLEMENTARY, not duplicated.
**D2 (pushing cancellation further) has no measurable headroom.**

### 5.3 Cache locality (D3)
Same revision probes at one position band:

```
first            92.21ms
repeat identical 77.26ms   <- NOT <1ms: NO request-level cache in backend
cursor +1        91.25ms
cursor -1        92.74ms
far position     76.74ms
new revision     91.78ms
```

Every variant lands in the same 77-92ms band: **the backend has ZERO
completion-cache locality** -- each request pays the full AST/index
pipeline regardless of history.

### Decision tree resolution (contract section 2 / EVS43 ¡ì5)

```
A context-sensitive?        NO   -> D1 closed
B stale backend work?       LOW  -> D2 wiring demoted (Layer-1 gate suffices)
C cache locality?           NONE -> client-side cache is THE ONLY path
                                    to <1ms candidates => EVS-5.2 D3 PROMOTED
D nothing actionable?       n/a
```

**Next concrete lever: ENCA-level completion cache (EVS-5.2 phase,
data contract already frozen in section 3).**  Backend stays a black
box we cannot and should not modify.

## 6.1 Post-fix re-run + a real bug found by attribution (2026-08-24)

The first 5.1 run exposed clangd-side `JSON parse error` for every
didChange frame.  Root cause: `enca_lsp_did_change_full` terminated
its payload with `}]}` -- one closing brace short (params closed, the
outer request object never did).  Windows builds never noticed
(WriteFile path unaffected; only the server's parser saw it).

Consequence: in EVS-4.3's storm-real harness the document updates
never reached clangd -- its responses were computed over STALE text.
Numbers from that harness are therefore re-measured below.

Fixed + re-run (raw stream: bench/results/evs5_backend_attribution.log):

```
CTXSWEEP 32B..32KB : p50 flat band 61.6 - 78.4 ms   (D1 falsified, again)
STALE burst        : 4/12 answered, rest coalesced server-side
STALE + cancel     : newest-only answering confirmed
CACHE all variants : 61.6 - 76.8 ms flat            (zero locality)
```

Decision tree unchanged and now measured over a CORRECT protocol:
D1 NO-GO, D2 no headroom, D3 (client-side cache) remains the only
promoted lever.  Suite: 34059 checks / 0 failures.
