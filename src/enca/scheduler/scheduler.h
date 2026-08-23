#ifndef ENCA_SCHEDULER_H
#define ENCA_SCHEDULER_H

/* P3.1 minimal task model + admission engine (contract:
   scheduler/SCHEDULER.md, ARCHITECTURE.md #20-#24).

   This phase owns: task records, class queues, admission decisions
   (accept / replace / fold / drop-expired), and dispatch-side
   staleness+expiry gates with drop accounting.

   NOT in this phase: worker threads, execution, result routing
   (P3.2); deeper scheduling (conditional on EVS-1 evidence). */

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../id/id.h"
#include "../thread/thread.h"
#include "../time/time.h"

#include <stdatomic.h>

/* Closed task classes -> one FIFO queue each.  Dispatch order is
   SYSTEM (lifecycle barrier) > INTERACTIVE > BACKGROUND >
   MAINTENANCE. */
typedef enum
{
  ENCA_TCLASS_SYSTEM = 0,
  ENCA_TCLASS_INTERACTIVE = 1,
  ENCA_TCLASS_BACKGROUND = 2,
  ENCA_TCLASS_MAINTENANCE = 3,
  ENCA_TCLASS_COUNT
} enca_task_class;

/* Closed urgency enum; orthogonal to deadlines (#21). */
typedef enum
{
  ENCA_URGENCY_REALTIME = 0,
  ENCA_URGENCY_INTERACTIVE = 1,
  ENCA_URGENCY_NORMAL = 2,
  ENCA_URGENCY_BACKGROUND = 3,
  ENCA_URGENCY_MAINTENANCE = 4,
} enca_urgency;

typedef enum
{
  ENCA_DROP_NONE = 0,
  ENCA_DROP_SUPERSEDED,     /* replaced by a newer same-domain task */
  ENCA_DROP_STALE,          /* epoch no longer current at dispatch  */
  ENCA_DROP_EXPIRED,        /* deadline passed before dispatch      */
  ENCA_DROP_SHUTDOWN,       /* queue drained at shutdown            */
} enca_drop_reason;

/* The task record itself.  Payload/execution hooks arrive in P3.2;
   this phase carries only scheduling semantics. */
typedef struct enca_sched_task
{
  enca_u64 task_id;
  enca_object_id document_id;      /* domain key part 1                */
  enca_task_class cls;             /* domain key part 2 + queue choice */
  enca_u64 generation;             /* epoch at submit                  */
  enca_u64 document_revision;      /* revision at submit               */
  void *snapshot_handle;           /* retained opaque P2 handle        */
  void (*release_snapshot) (void *handle);
  enca_deadline deadline_ns;       /* ENCA_DEADLINE_NONE allowed       */
  enca_urgency urgency;
} enca_sched_task;

typedef struct enca_sched_node
{
  enca_sched_task task;
  enca_timestamp_ns submitted_ns;   /* phase breakdown: submit time   */
  struct enca_sched_node *prev, *next;
} enca_sched_node;

typedef struct
{
  enca_sched_node *head, *tail;
  enca_usize count;
} enca_task_queue;

/* Admission outcome for a submitted task. */
typedef enum
{
  ENCA_ADMIT_ACCEPTED = 0,   /* queued                               */
  ENCA_ADMIT_REPLACED,       /* queued; older same-domain entries were
                                evicted (DROP_SUPERSEDED counted)    */
  ENCA_ADMIT_FOLDED,         /* NOT queued: an equal-or-newer task is
                                already admitted for this domain     */
  ENCA_ADMIT_DROPPED_EXPIRED,/* dead on arrival (deadline passed)    */
  ENCA_ADMIT_REJECTED        /* invalid argument / OOM               */
} enca_admit_result;

/* ---------------- P3.2: minimal executor types ---------------- */

/* Lifecycle state machine (shutdown is first-class):
   RUNNING -> STOP_ACCEPTING -> STOP_WORKERS -> JOINED. */
typedef enum
{
  ENCA_SCHED_RUNNING = 0,
  ENCA_SCHED_STOP_ACCEPTING = 1,
  ENCA_SCHED_JOINED = 2,
} enca_sched_state_enum;

/* Terminal status of a queued task.  Every admitted task produces
   exactly one result with exactly one status (accounting identity). */
typedef enum
{
  ENCA_TSTAT_EXECUTED = 0,
  ENCA_TSTAT_FAILED,
  ENCA_TSTAT_DROPPED_SUPERSEDED,
  ENCA_TSTAT_DROPPED_STALE,
  ENCA_TSTAT_DROPPED_EXPIRED,
  ENCA_TSTAT_DROPPED_SHUTDOWN,
} enca_task_status;

typedef struct enca_sched_result
{
  struct enca_sched_result *next;  /* FIFO link (result queue)         */
  enca_u64 task_id;
  enca_object_id document_id;
  enca_u64 generation;
  enca_u64 document_revision;
  enca_task_class cls;
  enca_task_status status;
  enca_timestamp_ns submitted_ns;
  enca_timestamp_ns admitted_ns;
  enca_timestamp_ns dispatched_ns;
  enca_timestamp_ns finished_ns;   /* phase breakdown for overhead */
  enca_u64 value;                  /* opaque execution output      */
} enca_sched_result;

/* Business-ignorant execution hook (#P3.2-5). */
typedef int (*enca_task_execute_fn) (const enca_sched_task *task,
                                     void *exec_ctx, enca_u64 *out_value);

/* Commit callback invoked from poll on the calling (main) thread.
   Epoch validation and any Emacs mutation happen HERE, not in the
   worker. */
typedef void (*enca_sched_commit_fn) (const enca_sched_result *res,
                                      void *ctx);

typedef struct
{
  _Atomic enca_u64 submitted;
  _Atomic enca_u64 accepted;
  _Atomic enca_u64 replaced;
  _Atomic enca_u64 folded;
  _Atomic enca_u64 dropped_expired_submit;
  _Atomic enca_u64 dropped_stale_dispatch;
  _Atomic enca_u64 dropped_expired_dispatch;
  _Atomic enca_usize queued[ENCA_TCLASS_COUNT];
  /* P3.2 executor accounting (identities in SCHEDULER.md section 9):
     submitted == rejected + folded + expired_submit + accepted
     accepted  == results_total == executed + failed + dropped_*      */
  _Atomic enca_u64 executed;
  _Atomic enca_u64 failed;
  _Atomic enca_u64 results_total;
  _Atomic enca_u64 shutdown_rejects;
} enca_scheduler_stats;

typedef struct enca_scheduler
{
  enca_mutex lock;
  enca_condition wake;                 /* workers wait for tasks     */
  enca_task_queue q[ENCA_TCLASS_COUNT];
  _Atomic enca_u64 next_task_id;
  _Atomic enca_u64 cur_gen;
  _Atomic int state;                   /* enca_sched_state           */
  enca_scheduler_stats st;

  /* Executor (P3.2). */
  enca_task_execute_fn exec_fn;
  void *exec_ctx;
  enca_thread *workers;
  unsigned n_workers;

  /* Completed results awaiting main-thread poll. */
  enca_mutex rlock;
  enca_sched_result *res_head, *res_tail;
  enca_usize res_count;
} enca_scheduler;

enca_result enca_sched_init (enca_scheduler *s);

/* Admission: applies the v1 policy (#22/#23).  On ACCEPTED or
   REPLACED the task is copied into the queue and *out_id receives
   its id; on every other outcome the caller keeps ownership of its
   own copy and nothing is stored. */
enca_admit_result
enca_sched_submit (enca_scheduler *s, const enca_sched_task *in,
                   enca_u64 *out_id);

/* Dispatch gate: pops the highest-priority non-dropped task.
   cur_gen / doc_rev_of define "current" for staleness checks; pass
   rev_fn == NULL to skip revision checks (tests/simple embeds).
   Returns false when nothing dispatchable remains. */
typedef enca_u64 (*enca_sched_rev_fn) (void *ctx, enca_object_id doc);

bool enca_sched_pop (enca_scheduler *s, enca_u64 now_ns,
                     enca_u64 current_generation,
                     enca_sched_rev_fn doc_rev_fn, void *rev_ctx,
                     enca_sched_task *out, enca_drop_reason *why);

/* Drain everything, counting DROP_SHUTDOWN per removed task.
     Returns number of tasks removed. */
enca_usize enca_sched_shutdown_drain (enca_scheduler *s);

void enca_sched_destroy (enca_scheduler *s);

const enca_scheduler_stats *enca_sched_stats (const enca_scheduler *s);


/* Start N worker threads.  exec_fn must not touch Emacs state. */
enca_result enca_sched_start_workers (enca_scheduler *s, unsigned n_workers,
                                      enca_task_execute_fn exec_fn,
                                      void *exec_ctx);

/* Advance the runtime epoch: every queued task becomes stale at its
   dispatch gate (drop-before-compute). */
void enca_sched_advance_generation (enca_scheduler *s);

enca_u64 enca_sched_current_generation (const enca_scheduler *s);

enca_sched_state_enum
enca_sched_get_state (const enca_scheduler *s);

/* Drain completed results and route them to the commit callback.
   Non-blocking; returns the number routed. */
enca_usize enca_sched_poll (enca_scheduler *s,
                            enca_sched_commit_fn commit_cb, void *ctx);

/* Shutdown sequence:
   STOP_ACCEPTING (submits rejected, DROP_SHUTDOWN counted)
   -> workers drain remaining queue as DROPPED_SHUTDOWN results
   -> STOP_WORKERS once queues empty -> JOIN.
   Returns after all workers are joined. */
void enca_sched_shutdown (enca_scheduler *s);

#endif /* ENCA_SCHEDULER_H */
