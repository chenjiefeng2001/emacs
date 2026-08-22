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

See `cancel/LIFETIME.md` for the cancellation-object lifetime protocol.
