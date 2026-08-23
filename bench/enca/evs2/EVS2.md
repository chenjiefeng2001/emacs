# EVS-2 -- Incremental Capture Contract

Status: **contract frozen before implementation** (same rule as every
phase).  North Star constraint applies: this phase exists because
EVS-1 measured the full-copy family (buffer-string + full snapshot)
as the dominant large-buffer cost on a real user path.

## 0. Goal (North Star form)

> Keep P2 snapshot semantics, revision/stale correctness and P3
> drop-before-compute EXACTLY as they are; eliminate the full text
> copy on the keypress -> capture -> snapshot path.

Success is user-path shaped:

```
large document + small edit + interactive frequency
    => capture cost down, keypress tail latency down
```

NOT "incremental benchmark goes up".

## 1. Frozen ABI (unchanged from P2/P3)

DocumentSnapshot, TextView, epoch {generation x revision}, lifetime,
admission, commit validation -- all UNTOUCHABLE.  Consumers still see:

```
Capture -> DocumentSnapshot -> TextView
```

Consumers must never see mutable chunks or require self-assembly.
Incremental changes HOW a snapshot is produced, never WHAT it means
(SNAPSHOT.md consistency-boundary rule).

## 2. Edit Delta model (v1: single contiguous range)

```
enca_edit_delta {
  document_id, base_revision, new_revision,
  start_byte, old_end_byte,        /* deleted span [start, old_end) */
  inserted_length                  /* payload replaces that span    */
}
```

- v1 supports ONE contiguous range per edit (matches
  after-change-functions beg/end/old-len).
- Offsets are CANONICAL BYTE OFFSETS into the base revision text.
  The Capture Adapter converts Emacs char positions immediately;
  nothing below the adapter ever sees Emacs positions (#17).

## 3. Emacs buffer is NOT shared memory

Workers read ENCA-owned immutable snapshots only.  No worker ever
touches buffer text, GC or locks.  Incremental happens in CAPTURE;
snapshot consistency semantics are unchanged.

## 4. Backend: persistent chunked document (v1)

Reuse of the P2.1 candidate shape:

```
Document state: ordered pieces (immutable chunk buf, off, len)
Edit: rewrite only the table around [start, old_end); allocate the
      fresh insert payload; untouched slices keep sharing buffers
```

Explicitly deferred until EVS-2 data demands them: rope, piece-table
variants beyond v1, B-tree, gap buffer, CRDT.

## 5. Correctness oracle (hard gate)

Every revision, for every cell:

```
hash(full_snapshot) == hash(incremental_snapshot)
length equal; spot ranges equal; revision equal
```

Plus lifetime sanity: superseded snapshots stay readable until their
last reference drops.

## 6. Offset semantics rule

Adapter receives Emacs-native positions and converts to canonical
byte offsets immediately.  Chunked storage understands ONLY canonical
byte offsets.  UTF-16/scalar conversion belongs to the future offset
index (#17), not to storage.

## 7. Core new metric: copy amplification

```
copy_amplification = physical_bytes_copied / logical_bytes_changed
```

- Full capture @10MB, 1B edit: ~10,000,000x (whole doc copied)
- Incremental target: ~O(chunk/payload), NOT required to be O(1)

Also tracked: capture latency, publish latency, RSS delta, allocation
count, plus the frozen correctness oracle.

## 8. Small-Edit/Large-Document invariant (success standard)

```
document=100MB, edit=1B:
  full capture   ~ O(N)          tens of ms
  incremental    ~ O(changed chunks + metadata)   sub-ms .. few ms
```

Content, revision, staleness and memory-boundedness must all hold.

## 9. Bench-only first

No Emacs-core changes until the bench proves incremental capture is
correct AND materially better on the sweep.  Integration order after
evidence: bench model -> src backend -> enca-evs adapter -> EVS-2
vertical slice re-run of E1/E3/E4.

## 10. Sub-phases

| Phase | Content | Gate |
|---|---|---|
| EVS-2.0 | this contract | approved |
| EVS-2.1 | bench sweep full-vs-incremental + amplification table | oracle green everywhere |
| EVS-2.2 | src backend integration behind existing APIs | suite green |
| EVS-2.3 | enca-evs adapter + E1/E3/E4 re-run | keypress tail improved at scale |
| EVS-2.4 | decision record + tag | written verdict |
