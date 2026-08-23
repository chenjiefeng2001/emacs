#ifndef ENCA_WAKE_H
#define ENCA_WAKE_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../thread/thread.h"

/* EVS-3 Runtime Notification primitive (bench/enca/evs3/EVS3.md).

   One-shot, coalescing "new main-thread work exists" signal.  It
   carries NO payload and knows NOTHING about Emacs, queues or
   schedulers; the consumer polls its own work source and uses this
   only to avoid sleeping through ready results.

   Protocol (strict; the mutex is what makes it a protocol):

     IDLE          token == 0   nothing outstanding
     NOTIFIED      token == 1   a wake is pending
     MAIN_DRAINING implicit    between a true wait() and the next
                               wait(); a notify() arriving here sets
                               NOTIFIED again, so the next wait()
                               returns immediately (no lost wakeup)

   Producer rule: enqueue the work FIRST, then notify.
   Consumer rule: drain until your poll returns zero, THEN wait();
   wait() re-checks the token under the mutex before blocking.

   Storm rule: any number of notify() calls between two waits collapse
   into at most one wakeup.  */

typedef struct
{
  enca_mutex m;
  enca_condition c;
  int token;                    /* 0 = IDLE, 1 = NOTIFIED           */
} enca_wake_source;

enca_result enca_wake_init (enca_wake_source *w);
void enca_wake_destroy (enca_wake_source *w);

/* Producer side (worker threads).  Wait-free under contention with
   the consumer is not required; a short critical section is fine.
   Leaf-safe: must never be called while holding this mutex. */
void enca_wake_notify (enca_wake_source *w);

/* Consumer side.  Blocks up to TIMEOUT_NS for a pending notification,
   consumes it and returns true; returns false on timeout with no
   pending notification (token untouched in that case).  */
bool enca_wake_wait (enca_wake_source *w, enca_u64 timeout_ns);

/* Non-blocking consume: true if a notification was pending. */
bool enca_wake_poll (enca_wake_source *w);

#endif /* ENCA_WAKE_H */
