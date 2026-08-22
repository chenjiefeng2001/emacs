# ENCA Runtime Architecture Contract

This file lists invariants that must never be violated.  It records
*what must hold*, not *how it is implemented*.  Any design or
optimization that conflicts with this contract requires an explicit,
written amendment here first.

1. Emacs objects may only be accessed by the interpreter/main thread.

2. ENCA worker threads operate only on native immutable inputs.

3. Worker threads must never call Elisp.

4. Generation identifies logical freshness, not lifetime.

5. Reference ownership determines object lifetime.

6. Event records facts; tasks perform work.

7. Results must be validated before commit.

8. Runtime shutdown must join all workers before destruction.

9. ENCA may be disabled without changing legacy behavior.

10. No optimization may increase main-thread p99 latency without
    explicit justification.

11. The Emacs main thread owns all Emacs state; ENCA workers own
    native runtime work only.

12. Workers must never hold or touch a Lisp_Object.  The only
    Lisp-visible surface is the main-thread bridge
    (`src/enca-emacs.c`).  Its primitive set is frozen at
    available-p/submit/set-handler/poll/cancel/status/shutdown;
    buffer, redisplay, timer-control or Lisp-eval primitives belong
    to later phases and require amending this contract first.

13. Worker termination must be observed (joined) from an Emacs-safe
    execution point on the main thread -- never from worker context,
    never by workers calling back into Emacs.  Fatal-signal teardown
    must not attempt to join.

14. Configured without `--enable-enca`, Emacs must build and behave
    exactly as upstream: no ENCA objects linked, no feature macros
    defined, no primitives visible.

15. Snapshot Semantic Contract.  A snapshot is an immutable
    observation of one document at one revision.  After publication,
    neither further edits nor newer revisions may affect a snapshot's
    bytes or metadata.  A `const` qualifier alone never establishes
    this: immutability is a property of ownership and publication,
    not of an access path.

16. Encoding / Canonical Representation.  Every snapshot exposes a
    canonical UTF-8 view built once at publication and cached.
    Consumers must not perform ad-hoc encoding normalization; source
    representation travels as metadata and may be materialized
    lazily.

17. Coordinate / Offset Contract.  Offsets are typed (byte, Unicode
    scalar, UTF-16, line/column) and converted only through the
    snapshot layer's central index.  A single integer type must never
    silently mean different units in different APIs.

18. Ownership / Lifetime Contract.  Snapshot lifetime is governed
    exclusively by reference counts and must cover every consumer's
    lifetime; it is independent of buffer lifetime.  The object
    registry provides identity, generation and type only -- never
    lifetime.  Workers receive already-retained pointers at submit
    time and perform no registry lookups, no locking and no
    acquisition while running.

19. Staleness / Commit Contract.  Results carry their full epoch:
    runtime generation AND document revision.  A result commits only
    if both match current values; either mismatch drops it as stale.
    Cooperative mid-task abort remains generation-scoped; correctness
    never depends on workers observing document revisions.

See `cancel/LIFETIME.md` for the cancellation-object lifetime protocol.
