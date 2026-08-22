# CancelSource Lifetime Contract

## Ownership model

`enca_cancel_source` lifetime is governed exclusively by reference
ownership (the `refs` field).  Generation identity and object lifetime
are orthogonal concerns:

- `generation` answers: "is this result stale?"
- `reference count` answers: "may this object be freed?"

A source whose generation has been superseded MUST stay alive as long
as any holder keeps a reference to it.

## Reference holders

1. The controller (e.g. `enca_runtime`) owns one reference for the
   slot it publishes (currently `gen_cancel`).
2. Every thread that observes the slot through publication order owns
   its own retained reference for the duration of its use.

Workers must never borrow an unretained source.  Observing the pointer
without retaining is forbidden; use the acquire protocol below.

## Publication / replacement protocol

Replacement of the current source happens under the runtime's
`state_lock`, which is also the lock used by the acquire side:

- Controller (`advance_generation`): lock, exchange slot to NULL,
  cancel + release the old source, store the fresh source, unlock.
- Consumer (`generation_cancelled`): lock, load slot, retain, unlock,
  use, release.

Because load+retain happen inside the same critical section that the
controller uses for exchange, a consumer either retains the old source
before the controller drops its own reference (object stays alive) or
observes NULL / the fresh source afterwards.

## Known-safe shortcuts

- `enca_cancel_source_is_cancelled(NULL)` is defined as `false`.
  Callers may treat a NULL slot as "no cancellation observed", or as
  cancelled, per policy -- but they must not dereference it.
- `enca_runtime_destroy` exchanges and releases `gen_cancel` without
  `state_lock`; this is only valid because all workers are joined
  before destroy.  Any future caller path with live workers must take
  the lock.

## Forbidden patterns

- Reading `src->state` or `src->parent` without holding a reference.
- Load-then-retain outside the publication lock (TOCTOU: the last
  owner may free between load and retain).
- Replacing generation identity as a substitute for releasing
  references.

## Regression vehicle

`test/enca/test_cancel_race.c` contains two suites:

- `cancel-race/borrow-unretained`: intentionally reproduces the
  forbidden pattern as a canary.  It is skipped in ASan builds unless
  `ENCA_TEST_FORCE_RACE_CANARY` is defined; building with that macro
  plus `-fsanitize=address` must yield a heap-use-after-free report,
  proving detector sensitivity.
- `cancel-race/retain-on-load`: hammers the sanctioned protocol and
  must stay clean under ASan.
