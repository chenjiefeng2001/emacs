# P2 Snapshot / State Isolation -- Design Contract

Status: **P2 CLOSED (archived at tag `enca-p2.1-storage-closure`).**
P2.0 implementation lives in `snapshot.{h,c}`; the storage decision
(hybrid flat+chunked behind a StoragePolicy, C2 deferred coalescing)
is recorded in `bench/REPORT.md` sections 11-12 with the frozen
baseline in `bench/results/p21-flat-baseline-v1/`.  Experiment
platform: `bench/enca/`.  Successor phase: P3 Scheduler (see
`scheduler/SCHEDULER.md`).

Guiding principle (advisor ruling):

> A snapshot is a *consistency boundary*, not a string-copy API.
> Every decision below derives from revision, ownership,
> immutability and the cost model -- never from convenience.

Core question this phase answers:

> How can a worker obtain a sufficiently complete, consistently
> observable view of editing state *without ever operating on an
> Emacs buffer*?

## 1. Problem statement

Buffers are mutable, main-thread-owned, GC-managed.  Workers may only
see native immutable data (ARCHITECTURE.md #1-#3, #11).  P2 introduces
the missing object: an immutable observation of document state at a
specific revision, published once on the main thread, shared by many
consumers.

    Emacs Buffer
         |
    Capture Adapter          (source repr / encoding / positions)
         v
    Document State           {revision, source metadata, text}
         |  publish()
         v
    Full Snapshot            {canonical UTF-8, mapping, revision,
         |                    lifetime}
     +---+---+---+
     v   v   v
   Parser Completion Diagnostics        (all see identical bytes)

Principle: **capture once, normalize once, publish once, share many** --
never "normalize N times, copy N times".

## 2. Non-goals (until amended)

- No write-back to buffers.
- No overlays / intervals / text properties.
- No Elisp-visible object types; no new bridge primitives
  (`src/enca-emacs.c` stays frozen per ARCHITECTURE.md #12; the
  Capture Adapter and publishing glue arrive as an explicit P2.x
  contract amendment).
- No rope / piece table / persistent tree selection in P2.  Storage
  evolution is benchmark-driven (see gates); semantics are fixed
  first, representation evolves underneath.
- No multi-threaded UI, no GC interaction.

## 3. Object model

```c
typedef struct enca_document_snapshot enca_document_snapshot;

struct enca_document_snapshot
{
  _Atomic enca_u32 refs;          /* lifetime = refcount              */

  enca_object_id   self_id;       /* ENCA_OBJ_SNAPSHOT registry slot  */
  enca_object_id   document_id;   /* logical source                   */
  enca_u64         revision;      /* monotonic per document_id        */

  /* --- required at publish ------------------------------------- */
  const unsigned char *utf8;      /* CANONICAL consumer representation*/
  enca_usize        utf8_len;
  enca_text_index_t *index;       /* centralized offset mapping       */

  /* --- source metadata (always present) ------------------------ */
  enca_encoding_t   source_encoding;
  enca_flags_t      flags;        /* provenance bits                  */
  enca_u64          capture_ns;

  /* --- optional, lazily materialized ---------------------------- */
  const unsigned char *raw;       /* source representation, lazy      */
  enca_usize        raw_len;
};
```

Decisions encoded above:

1. **Canonical UTF-8 view is REQUIRED at publish**, built once and
   cached inside the snapshot.  Not per-consumer.
2. **Raw source representation is OPTIONAL/LAZY** -- retained as
   metadata first; materialized only when a consumer genuinely needs
   byte-exact source access.  Never force two full copies per edit.
3. **Consumers must never re-normalize.**  Different parsers each
   converting bytes -> UTF-8 -> codepoints independently is how offset
   semantics die.  Encoding normalization belongs to the snapshot
   layer, written down as contract (section 7).

### Identity, staleness, lifetime

- Identity of freshness = `(document_id, revision)`; identity of the
  object = generational registry slot (`ENCA_OBJ_SNAPSHOT`, existing
  `enca_id_registry`).  "Logical identity != object lifetime".
- Lifetime is refcount-governed, mirroring `enca_cancel_source`
  (`cancel/LIFETIME.md`): refs answer "may this be freed?",
  revision answers "is this result stale?".
- **Snapshot lifetime is independent from buffer lifetime.**  A
  superseded snapshot stays readable until its last ref drops.
- Two-level staleness: runtime generation (global, exists today) AND
  per-document revision (new).  A result commits only if both match.

## 4. Ownership and the registry (design-debt resolution)

Header-tag pointer probing (technical UB) is replaced by the reserved
generational id machinery:

- Each live snapshot owns one `ENCA_OBJ_SNAPSHOT` slot.
- Validation goes through `enca_idr_is_alive`, never tag reads.
- Registry mutation happens on the publishing thread only.  Workers
  receive already-retained pointers at submit time and touch neither
  locks nor registry while running.

Shutdown asserts zero live snapshots after workers join (leak canary).

## 5. Publication / acquisition protocol

Same shape as `gen_cancel` in `cancel/LIFETIME.md`:

```
Publisher (main thread):
    publish_lock { old = slot[doc]; slot[doc] = fresh; rev++; }
    release old when refcount drops

Acquirer (main thread only, submit time):
    publish_lock { retain(slot[doc]) }  -> attach to task_input
    worker reads snapshot bytes lock-free
    runtime releases task's ref on every disposal path
```

Required runtime change (small, generic): `enca_task_input` gains an
optional destructor hook so the runtime can release payloads it does
not understand.  No worker-visible semantic change.

## 6. Offset contract (typed, centralized)

One `size_t` must never silently mean different things in different
APIs.  Offsets are typed; conversion is centralized in the snapshot
index:

```c
typedef struct { enca_usize byte;   } enca_byte_offset;
typedef struct { enca_usize scalar; } enca_scalar_offset;
typedef struct { enca_usize utf16;  } enca_utf16_offset;

typedef struct { enca_byte_offset start, end; } enca_byte_range;
```

Canonical chain maintained by `enca_text_index_t`:

```
UTF-8 byte <-> Unicode scalar <-> line/column <-> UTF-16
```

UTF-16 support matters because LSP speaks UTF-16 column offsets;
getting this wrong late costs far more than typing it early.

## 7. Snapshot contract (proposed ARCHITECTURE.md amendment, #15+)

    15. Snapshots are immutable after publication.
    16. Snapshots carry a stable per-document revision, and results
        carry the revision they were computed against.
    17. The snapshot layer owns the canonical text representation;
        consumers must not perform ad-hoc encoding normalization or
        private offset conversion.
    18. Range views derive from a snapshot and never mutate it.
    19. Snapshot lifetime is independent from buffer lifetime and
        governed solely by reference ownership.

## 8. Emacs-side capture boundary

Do NOT assume "buffer bytes == UTF-8".  Emacs has multibyte and
unibyte representations, char vs byte positions, and raw-byte
escapes.  The **Capture Adapter** (main thread, future P2.x glue) is
the single place that knows about these; everything below it sees
only `enca_document_snapshot`.  This adapter is where buffer-internals
knowledge stops leaking into the runtime.

## 9. Granularity: Full-Document API, not Full-Document Copy

v1 exposes a whole-document snapshot API.  This binds the SEMANTICS
(consistent view of the entire document at one revision), not an
implementation requirement to memcpy the buffer every publish.

- v1 implementation may be a flat contiguous capture (simplest thing
  that proves the model).
- The API must not make chunked/shared/lazy internals observable.
- P2.2 range snapshots are VIEWS `(snapshot, start, end)` -- zero
  copy, O(chunks) construction, results still carrying
  `(document_id, revision, range)`.  They extend full snapshots;
  they do not replace them.

## 10. Sub-phase gates

| Gate | Content |
|---|---|
| P2.0 | This contract + ARCHITECTURE amendment (#15-#19). Native unit tests: capture/publish/acquire/refcount/two-level-stale accounting; registry leak canary. Flat-storage implementation permitted; API must not leak flatness. Verify: edit-after-publish leaves snapshot N valid, unchanged, readable. |
| P2.1 | Incremental publication research. Benchmark matrix 1 KB .. 1 GB x edit frequency x worker throughput measuring capture latency / bandwidth / RSS BEFORE choosing flat vs chunked vs rope vs piece table vs persistent tree. Data decides, not fashion. |
| P2.2 | Bridge amendment (`enca-publish-*`) + batch E2E from Elisp: edit -> publish -> submit -> further edits -> stale accounting observed. Range View API if consumers exist. |
| P2.3 | Storage decision implemented per P2.1 data (likely chunked immutable sharing); sanitizer matrix; new bench baseline record separate from frozen P1 baseline. |

## 11. Explicitly unchanged

- Worker threads never see Lisp_Object or call Elisp.
- Main thread remains sole owner of all Emacs state.
- P1.11 bridge primitive set stays frozen until P2.2 amends the
  contract in writing.

## 12. Lifetime Protocol (P2.0, frozen before implementation)

Object relation and reference flow:

```
Emacs Buffer
     |  capture (main thread only)
     v
Document  --publish-->  Snapshot
                          |
                          | acquire (+1) at submit
                          v
                      TaskInput --- worker reads view (no locks)
                          |
                          | enca_task_input_destroy (ALL paths)
                          v
                       release (-1) ---> [0] destroy
```

Rules (each is testable; see gate matrix):

L1. `publish` = capture + canonicalize + index + immutable
    publication.  `acquire` = refcount increment ONLY.  The two are
    never merged: nothing that publishes may be needed to share.

L2. Every pointer handed out carries exactly one reference:
    publish returns a publisher-owned ref (also stored as the
    document's slot), acquire/latest return new refs.  Release
    pairing is explicit at every call site.

L3. `Snapshot lifetime >= every consumer lifetime`.  NOT "buffer
    lifetime >= snapshot lifetime": documents advance and may be
    destroyed while superseded snapshots remain valid until their
    last reference drops (S13).

L4. Epoch is two distinct numbers (`enca_snapshot_epoch`:
    runtime_generation, document_revision).  Never merge them into
    one scalar; results carry the full epoch so commit-side
    validation cannot forget a level.

L5. Registry = identity/generation/type ONLY.  Slots are allocated
    on the publishing thread at creation.  Slot RELEASE is deferred:
    when the last reference drops on any thread, the snapshot enters
    a lock-free pending stack (wait-free push, no locks); only the
    publishing thread pops it (enca_snap_reclaim) and frees the
    registry slot and memory.  Lookup never retains, retention never
    looks up.  No thread other than the publisher ever touches the
    registry or takes a lock.

L6. Task payload destruction has ONE entry point,
    `enca_task_input_destroy`, honoring an optional per-task
    destructor hook.  All four disposal paths -- normal completion,
    cooperative cancellation, engine stale drop, shutdown drain --
    route through it.  No path frees payloads by hand.

L7. Invariant counters (created/published/acquired/released/
    destroyed/live) must satisfy `created - destroyed == live` once
    reclamation has run, and shutdown requires `live == 0` with every
    retired snapshot reclaimed.

P2.0 scope guards: no range views, no chunked storage, no rope /
piece table, no incremental UTF-8, no parsers, no LSP offsets beyond
the typed-offset contract, no redisplay or threading changes.
