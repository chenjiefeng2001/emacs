# EVS-2.4 Decision Record -- Incremental Capture Backend

Status: **frozen 2026-08-24**.  EVS-2.2 is closed as a formal P2
backend candidate.  Storage work STOPS here; the only permitted next
step is the EVS-2.3 adapter A/B on the real Emacs user path (section
8).  Contracts: ../ARCHITECTURE.md #15-#19, EVS2.md section 10.1.

## 1. Incremental capture is a Capture Strategy

Incremental capture did NOT create a new Snapshot ABI.  Consumers
still see exactly one pipeline:

```
Capture Adapter
    |
    +-- Full Capture          (P2 flat path, untouched)
    +-- Incremental Capture   (new)
             |
             v
        DocumentState      (piece-backed revision history)
             |
             v
          Snapshot         (identical external semantics)
```

Acquire/release, epoch {generation x revision}, commit validation,
TextView walking and drop-before-compute admission are byte-for-byte
the same contract as the flat path (#EVS-2.2.1: storage kind never
leaks).  The correctness oracle (hash(full_snapshot) ==
hash(incremental_snapshot) per revision) held on every revision in
every suite.

## 2. Frozen storage shape and OWNERSHIP CONTRACT (v1)

```
Snapshot
  +- immutable slice table   (refcounted, one object per revision)
        +- immutable pieces  (refcounted buffers, shared across tables)
```

Ownership is explicit at BOTH levels:

```
DocumentState --retain--> current revision's table
Snapshot      --retain--> its own revision's table
table         --retain--> every piece buffer it slices
```

**Hard rule (architectural, learned from a real UAF):** a table or a
piece must never be shared by pointer assignment alone.  This exact
shape was found in the wild during integration and is banned:

```c
/* BANNED: aliasing without retain.  Superseding the doc state then
   frees a table that live snapshots still reference. */
snapshot->table = ds->table;
```

Every published revision owns a DISTINCT table object that shares
piece BUFFERS (not the table) with other revisions.  Superseding an
edit releases only the DocumentState's table reference; snapshots of
earlier revisions keep theirs (#18/#EVS-2.2.4).

**Piece immutability:** once a piece is reachable from any published
snapshot it is never mutated in place.  Edits produce new tables
around a fresh insert payload; deleted middle pieces simply lose
their references.  Consequence (proven, not assumed): Snapshot(N)
remains byte-for-byte readable after revisions N+1 .. N+10000 --
see the retention torture evidence in section 6.

Deferred and still deferred: rope, B-tree, gap buffer, CRDT,
coalescing/compaction on the interactive path (future MAINTENANCE
policy class consumes piece_count / avg_piece_size).

## 3. Publication and lifetime contract

Each `enca_doc_state_edit` publishes exactly one registry-bound
snapshot for the new revision.  There is NO publisher slot on this
path; the reference algebra is therefore:

```
out != NULL : the creation reference IS handed to the caller
              (*out receives the only reference; release normally)

out == NULL : fire-and-forget publication.  The creation reference is
              dropped IMMEDIATELY, pushing the snapshot onto the
              normal pending_reclaim stack; the publishing-thread
              sweep (run at every edit entry) frees it and its
              registry slot.
```

"Fire-and-forget" means ownership TRANSFERRED TO THE RUNTIME -- not
absence of ownership.  The original implementation leaked one
reference per out==NULL publication, permanently pinning registry
slots; this is now structurally impossible because the creation
reference has exactly one owner on every path.

Registry slots are freed ONLY inside enca_snap_reclaim (SNAPSHOT.md
L5 two-phase destruction, unchanged).

## 4. Accounting invariants (hard gates)

```
created == published                  (every registry-bound snapshot)
rejected deltas never reach creation  (rejection precedes registration,
                                       so slots are never consumed by
                                       failed captures)
shutdown gate:  live == 0  AND  created == destroyed
                AND registry back to its pre-run baseline
```

The shutdown gate is enforced as CHECKs in test_dstate.c
(dstate/torture), including registry liveness via
enca_idr_live_count.

## 5. Metrics

Per doc state: length, piece_count, avg_piece_size, alloc_bytes
(cumulative payload bytes copied into fresh pieces -- allocation-
pressure proxy; sharing benefit is derivable as alloc_bytes vs
length).  Per snapshot: capture_ns (same field as the flat path).
System-wide: the standard enca_snap_stats counters.

Copy amplification stays a BENCH metric (EVS-2.1 harness); RSS stays
a bench-harness measurement -- the platform test runner cannot
measure RSS portably, so unit-level memory evidence uses alloc_bytes
growth bounds instead.

## 6. Evidence log (2026-08-24, clang C23 -O1, Windows x64)

- Full suite: 24178 checks, 0 failures (native).
- Retention torture (dstate/torture): deterministic PRNG, 1000 x
  1-byte random edits over an N-byte document, snapshots retained at
  strides 8/32/128, anchored at hold time (byte windows for all kept
  revisions + full-content FNV for the stride-128 subset), re-verified
  hundreds of revisions later; periodic full-hash verification of the
  current revision every 100 edits; exact lifecycle closure CHECKs.
  - default 8 MB:  125 retained, pieces=1997, avg=4200.6,
    alloc_bytes=8389607 vs len=8388608 => total extra payload over
    the whole run = 999 bytes (= exactly the 999 edit payloads);
    594 ms wall including all verification walks.
  - formal 100 MB: 125 retained, pieces=1999, avg=52455.0,
    alloc_bytes=104858599 vs len=104857600 (+999 bytes); 7830 ms
    wall.  Growth is O(edits), NOT O(edits x document): lifecycle
    correctness did not buy unbounded memory.
- ASan: full suite clean, zero reports (exit 0).
- LSan: unsupported on the x86_64-pc-windows-msvc target; lifecycle
  closure is proven by the section-4 identity instead (created ==
  destroyed, live == 0, registry baseline) which cannot pass with
  leaked snapshots.
- TSan: -fsanitize=thread unsupported for this target (clang);
  deferred to a Linux environment.  This phase's write surface is
  publisher-thread-only; the shared pending_reclaim stack it relies
  on is already exercised under load by the cancel-race and
  shutdown-storm suites.
- UBSan: local toolchain link failure, pre-existing, unrelated.

## 7. Stop conditions (reaffirmed)

None of rope / B-tree variants / coalescing / lock-free structures /
NUMA / zero-copy-from-Emacs may be adopted without a written
amendment here citing EVS-2.3 USER-PATH evidence.  Bench-internal
improvement alone never justifies reopening storage.

## 8. Next: EVS-2.3 A/B protocol (storage is frozen)

EVS-2.3 answers exactly one question: does the real user path get
faster?  It contains NO storage research.

```
A = EVS-1 Full Capture        B = EVS-2 Incremental Capture
E1 idle typing, E3 revision storm, E4 large document
matrix: {1MB, 10MB, 100MB} x {1B, 10B, 100B, 1KB}
report: keypress->visible p50 / p95 / p99 / p99.9 / max
        capture latency, copy amplification, RSS delta
```

GO: on large-document + small-edit, capture latency AND
keypress->visible tail improve materially with acceptable RSS and
clean sanitizers => EVS-2 success; proceed to Real Completion.

NO-GO (metric-misalignment guard): if capture improves ~100x while
keypress->visible does not move beyond noise, STOP optimizing
snapshot storage permanently -- the bottleneck has moved to Elisp
buffer mutation / redisplay / main-thread polling / completion, and
further piece-table work would be misdirected effort.

Phase tag: `enca-evs2-incremental-storage`.

## 9. Outcome appendix -- EVS-2.3 executed (2026-08-24): NO-GO

The section-8 protocol ran on a real Emacs build (WSL, --batch),
identical harness both arms, full matrix {1,10,100MB} x {1B..1KB}
plus E1/E3.  Results (bench/REPORT.md section 18):

- Capture latency: up to ~19,000x better at 100MB; amplification
  exactly 1.000 on the incremental arm.
- keypress->visible analog: NOT improved (+7..12% worse).  The
  dominant cost is the synthetic full-document analysis plus ~20ms
  batch-poll quantization; the piece-walk read makes the analysis
  slower than it saves.

The pre-committed NO-GO guard fired exactly as written: capture got
>100x cheaper while the user path did not move.  Per this decision,
snapshot storage optimization is permanently closed; no amendment
may reopen it based on storage-internal metrics.  A future consumer
with region-scoped analysis semantics would be a NEW vertical slice
(task/analysis model), not storage work.
