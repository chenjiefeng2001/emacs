# EVS-4 -- Real Completion Vertical Slice
# Section 4.0: COMPLETION CONTRACT (frozen before implementation)

Status: **contract frozen 2026-08-24**.  EVS-4 is NOT an LSP
integration project.  The first slice uses a REAL completion workload
shape with a SYNTHETIC completion server, so that language-server
latency, JSON-RPC transport and server scheduling cannot pollute the
experiment.

North Star metric (the only one that ranks decisions):

```
keypress -> useful visible result      (p50 / p95 / p99 / p99.9)
```

## 1. Why this phase exists (evidence chain)

EVS-3 removed the measurement floor and exposed the true remaining
cost: WORK, not runtime.  E4-100MB showed ~400ms of per-revision
full-document synthetic analysis; E1-incremental showed keypress->
commit at 0.8us when the work is region-shaped.  Completion is the
first REAL user path whose work is naturally region-scoped:

```
100MB document, cursor at 73MB, user types "foo."
    -> what a completion actually needs:
       prefix + receiver/call context  (hundreds of bytes .. few KB)
    -> NOT [0 .. document-size]
```

EVS-4 therefore tests Work Shaping / Work Avoidance on the user path,
under the standing constraint set of ARCHITECTURE.md section 25.

## 2. Frozen surface (unchanged from section 25)

P1 Runtime / P2 Snapshot ABI / P2 storage policy / P3 scheduler / EVS-3
wakeup: FROZEN.  Incremental capture stays CLOSED/NO-GO.  Any proposal
to touch these requires proof that they contribute a measurable share
of `keypress -> useful visible result`.

## 3. Completion Request model

```c
enca_ct_request {
  document_id, generation, revision   /* staleness domain            */
  cursor                              /* canonical byte offset       */
  trigger_kind                        /* manual/prefix/member/arg/syn*/
  prefix                              /* copied into ENCA memory     */
  ctx_start, ctx_end                  /* requested context range     */
  snapshot                            /* owned reference             */
}
```

Rules:

1. A completion request does NOT get the document.  It gets a
   SNAPSHOT reference plus its declared CONTEXT RANGE.  Access to the
   range goes through the EXISTING generic text walk
   (`enca_snapshot_walk_text`).  No new snapshot API.
2. Range View / OffsetIndex remain BANNED until measurements show the
   walk-based region extraction is an actual bottleneck (demand ->
   evidence -> API).  Extraction cost is a first-class metric here so
   this gate can be evaluated honestly.
3. Offsets are canonical byte offsets with memmove-style clamping,
   identical to EVS-2 semantics.
4. Stale requests follow #19/#22/#23 unchanged: newer same-domain
   request supersedes queued ones; drop-before-compute applies.

## 4. Workloads (frozen)

| id | shape                | input near cursor | context window |
|----|----------------------|-------------------|----------------|
| W1 | prefix               | `foo.ba\|`        | <= 32B         |
| W2 | member               | `object.foo\|`    | <= 256B        |
| W3 | argument             | `foo(arg1, \|`    | <= 1KB         |
| W4 | syntax-sensitive     | `if (...) { fo\|` | <= 4KB         |

Document sizes (cursor always at middle):

| id  | size  |
|-----|-------|
| C1  | 64KB  |
| C2  | 1MB   |
| C3  | 10MB  |
| C4  | 100MB |

## 5. Synthetic server

Deterministic candidate synthesis from the extracted region only
(FNV-seeded symbol shapes).  No network, no processes, no JSON.  The
server stands in for ranking/backend cost so pipeline numbers isolate
ENCA + Emacs overheads.  Real LSP arrives in EVS-4.3 ONLY after the
synthetic slice proves the workload shape on the user path.

simd-json / tree-sitter / lock-free structures / shared memory are
explicitly OUT OF SCOPE until evidence demands them.

## 6. Metrics

Per request: extract_ns, serve_ns, submit->result ns.
Aggregates: p50/p95/p99/p99.9 of keypress->request, keypress->
candidate-ready (= commit in the synthetic slice), and attribution of
the remaining gap to Completion UI (deferred to 4.4).
Wasted-completion accounting reuses the scheduler identity:
submitted == accepted+folded+expired+rejected; accepted == executed+
failed+drops; storm experiments report executed/submitted by typing
rate.

## 7. Sub-phases and gates

| Phase | Content                                            | Gate |
|---|---|---|
| 4.0 | this contract                                       | frozen |
| 4.1 | completion module + native vertical slice; C1-C4 x W1-W4 sweep; O(region) budget check | oracle green; extraction budget green |
| 4.2 | completion storm vs typing interval                 | admission table produced; drop-before-compute holds |
| 4.3 | real LSP (single target: clangd)                    | synthetic slice already proved workload shape |
| 4.4 | completion UI / redisplay attribution               | keypress->visible decomposed |
| 4.5 | decision closure (A fast enough / B backend-bound / C redisplay-bound) | written verdict |

## 8. Stop rules

- If C4 extraction via walk is < 5% of request latency: Range View
  stays banned, permanently for v1.
- If storm shows executed/submitted ~ 1 at high typing rates:
  admission is proven for IDE workloads; do not "optimize" it further.
- If candidate-ready lands sub-ms at all sizes while UI/redisplay
  dominates: branch C of 4.5 opens and Runtime work stays closed.

## 9. Outcome -- sections 4.1/4.2 executed (2026-08-24)

- Extraction budget gate (section 8): 256B region at cursor-middle
  costs 1.46us (1MB) / 2.75us (8MB) / 2.66us (10MB) / 3.14us (100MB)
  over a fragmented piece table (~800 pieces).  Document size grew
  100x; cost stayed flat at microsecond level.  Range View remains
  banned with evidence: region reads sit four orders of magnitude
  below any perceivable latency.
- Storm gate: with zero backend delay the queue never backs up
  (executed 12/12 at every paced interval; true storm executes 1/12).
  With simulated clangd-class latency (20ms), supersession removes
  queued duplicates while IN-FLIGHT requests still complete (9/12 at
  2ms typing) -- matching #23 exactly: in-flight protection belongs
  to cooperative cancellation, to be wired when real LSP lands in
  4.3.
- Oracle: identical regions extracted from flat and piece-backed
  storage match byte-for-byte across 21 window combinations.
- Full suite: 31750/0 native, ASan clean, TSan (WSL gcc) 0 warnings /
  31747 / 0.

Next: EVS-4.3 single-target clangd integration; preconditions met.
