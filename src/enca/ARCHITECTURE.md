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

See `cancel/LIFETIME.md` for the cancellation-object lifetime protocol.
