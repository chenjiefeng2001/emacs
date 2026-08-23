# EVS-4.3 Contract -- Real LSP Transport Attribution

Status: **frozen 2026-08-24, before implementation.**  Amends and
narrows bench/enca/evs4/COMPLETION.md section 7 (phase 4.3).

## 0. The question this phase answers

Not "can we integrate clangd?" but:

> What fraction of `keypress -> candidate-ready` is JSON-RPC framing +
> pipe I/O, what fraction is the backend, and does any of it justify
> SIMD JSON / shared memory / exotic IPC?

## 1. Hard constraints

- P1/P2/P3/EVS-2/EVS-3 contracts are UNTOUCHABLE.  clangd's interface
  habits must not leak into Snapshot, Scheduler, Cancellation, Runtime
  or the Completion request model.
- The Scheduler never learns the words clangd/JSON/LSP/stdio.  The LSP
  layer lives BELOW the executor hook: `execute(task) -> completion
  executor -> LSP transport`.
- Exactly ONE backend (clangd), one instance, one workspace.  Multi-
  server support is out of scope permanently for v1.
- Session lifecycle is INDEPENDENT of requests: spawn + initialize +
  didOpen happen at session setup; per-request cost must never include
  process lifecycle.
- NO simd-json, tree-sitter, shared memory, custom IPC in v1.  The
  first JSON parser/serializer is the smallest auditable hand-rolled
  one that can frame our messages and extract the few fields the gate
  needs (`id`, presence of `result`).  Evidence may justify SIMD later
  (EVS-4.x amendment); nothing justifies it now.

## 2. New invariants

### 2.1 Version mapping

```text
LSP textDocument.version  ==  ENCA document revision
```

One monotonic sequence, explicitly shared.  Independent counters are
forbidden.

### 2.2 Commit eligibility (THE correctness mechanism)

```c
commit_eligible =
       response.document_id == current.document_id
    && response.generation  == current.generation
    && response.revision    == current.revision
    && !cancelled
```

A response that fails ANY term is dropped before UI, regardless of a
clean LSP round trip.

### 2.3 Two-layer cancellation

Layer 1 (correctness): the revision/generation gate above.  Always
active; cannot fail open.
Layer 2 (optimization): `$ / cancelRequest` notification.  Best
effort; losing it, or clangd ignoring it, never breaks correctness.
Backend work wasted on stale in-flight requests is ACCOUNTED as
backend_waste either way.

## 3. Experiment design: three arms

```
B0  synthetic            direct function call (EVS-4.1 path)
B1  loopback             JSON-RPC serialize -> REAL OS pipe -> parse ->
                         deterministic synthesized response
                         (no server process)
B2  clangd               same path, real server process
```

Attribution identities:

```
B1 - B0  = JSON-RPC + framing + pipe overhead
B2 - B1  = clangd processing + server-side scheduling
```

Measurable timeline per request (T6 "server received" is NOT directly
observable without server timestamps; the honest decomposition is):

```
T0 submit            T4 serialized        T8 parsed
T1 request built     T5 write done        T10 commit decision
T2 context extracted T5..T7 pipe round trip + server think
T3 admitted          T7 frame complete
```

Reported segments: context-extract, serialize, write, roundtrip(+server),
parse.  All phases carry ns timestamps taken around the calls; percentiles
p50/p95/p99/max over K requests.

Document constraint (scope, recorded honestly): benchmark documents are
ASCII and newline-free, cursor expressed as line 0 / character = byte
offset; UTF-16 column mapping belongs to the future offset index (#17)
and is deliberately out of scope.

Setup costs (spawn/handshake/didOpen of up to 100MB) are measured ONCE
and reported separately from steady-state request latency.

## 4. Storm extension (real backend)

Typing burst `f fo foo ... ` with didChange(version=k) + completion per
keystroke against the live session.  Counters:

```
submitted admitted started lsp_sent responses_received
response_stale committed admission_waste backend_waste useful
```

waste split replaces the single wasted-work ratio:
admission_waste = dropped before start; backend_waste = started or
responded but stale/not committed; useful = committed.

## 5. Go / No-Go rules (frozen now)

```
GO-Transport:     transport share < 10% of candidate-ready p50
                  => JSON/IPC declared non-bottleneck; simd-json and
                  friends stay banned without NEW evidence.
GO-Backend:       clangd processing > 50%
                  => next work is backend/context strategy (4.3b/4.4).
GO-Cancellation:  only if $/cancelRequest demonstrably reduces
                  backend_waste; otherwise Layer 1 alone ships.
GO-UI:            commit->visible dominates tails
                  => EVS-4.4 / redisplay becomes the main battlefield.
```

## 6. Forbidden in 4.3 (reaffirmed)

Range Snapshot API, rope/piece-table tuning, work stealing, NUMA,
affinity, SIMD JSON, tree-sitter, multi-threaded redisplay, multiple
LSP servers, ranking optimization.  None has evidence today.

## 7. Sub-deliverables

```
4.3a  contract (this file)
4.3b  lsp session/transport abstraction + jsonrpc minimal codec
4.3c  loopback arm (B1) + revision-gate regression matrix
4.3d  clangd arm (B2, auto-detected, suite SKIPS cleanly when absent)
4.3e  attribution harness + storm-vs-real-backend
4.3f  closure record
```

## 8. Outcome -- sections 4.3a-4.3e executed (2026-08-24, clangd 19.1.0)

Attribution (1MB ASCII doc, cursor middle, K=20/200):

```
B0 synthetic direct          ~2us      (EVS-4.1 numbers)
B1 loopback  avg round trip  8.12us    serialize 2.5us + parse 6.7us + pipe
B2 clangd    p50 round trip  77ms     (p95 108ms; setup: spawn+init 83ms,
                                       didOpen 1MB 8ms)
```

Verdict against the frozen rules (section 5):

- GO-Transport FIRED: transport share of candidate-ready p50 is
  ~0.01%.  JSON-RPC framing + OS pipe costs single-digit microseconds.
  simd-json / shared-memory IPC are now PERMANENTLY unjustified by
  this data unless the workload changes shape.
- GO-Backend FIRED: clangd processing >99% of candidate-ready.
  The original "kill JSON at the Emacs boundary" thesis improved that
  boundary by orders of magnitude but is NOT where modern-IDE latency
  lives; the backend and the UI own it.
- Cancellation: $/cancelRequest implemented (Layer 2); storm harness
  runs synchronously so admission waste cannot express itself there --
  async in-flight cancellation accounting moves to 4.4 with the real
  UI loop (recorded honestly).
- Suite state: 31978 checks / 0 failures native (storm-real variant
  31995/0), ASan clean.

Next: EVS-4.4 completion UI / redisplay attribution -- per section 5
GO-UI will be evaluated with commit->visible measurements.
