#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "scheduler.h"

#include "../base/assert.h"
#include "../memory/memory.h"

#include <string.h>
#include <stdio.h>

enca_result
enca_sched_init (enca_scheduler *s)
{
  if (!s)
    return ENCA_ERR_INVALID_ARGUMENT;
  memset (s, 0, sizeof *s);
  enca_result r = enca_mutex_init (&s->lock);
  if (ENCA_RESULT_IS_ERR (r))
    return r;
  r = enca_mutex_init (&s->rlock);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_mutex_destroy (&s->lock);
      return r;
    }
  r = enca_condition_init (&s->wake);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_mutex_destroy (&s->rlock);
      enca_mutex_destroy (&s->lock);
      return r;
    }
  atomic_store (&s->next_task_id, 1);
  atomic_store (&s->cur_gen, 1);
  atomic_store ((atomic_int *) &s->state, ENCA_SCHED_RUNNING);
  return ENCA_OK;
}

const enca_scheduler_stats *
enca_sched_stats (const enca_scheduler *s)
{
  return &s->st;
}

static enca_usize
queue_push (enca_task_queue *q, enca_sched_node *n)
{
  n->next = NULL;
  n->prev = q->tail;
  if (q->tail)
    q->tail->next = n;
  else
    q->head = n;
  q->tail = n;
  return ++q->count;
}

static enca_sched_node *
queue_unlink (enca_task_queue *q, enca_sched_node *n)
{
  if (n->prev)
    n->prev->next = n->next;
  else
    q->head = n->next;
  if (n->next)
    n->next->prev = n->prev;
  else
    q->tail = n->prev;
  n->prev = n->next = NULL;
  q->count--;
  return n;
}

/* Push a HEAP-ALLOCATED result onto the FIFO and account it.
   Ownership of r transfers to the scheduler.  All accounting reads
   happen BEFORE the unlock: after that the main thread may already
   have polled and freed r (TSan-verified race). */
static void
sched_push_result (enca_scheduler *s, enca_sched_result *r)
{
  enca_task_status st = r->status;

  r->next = NULL;
  enca_mutex_lock (&s->rlock);
  if (s->res_tail)
    s->res_tail->next = r;
  else
    s->res_head = r;
  s->res_tail = r;
  s->res_count++;
  atomic_fetch_add (&s->st.results_total, 1);
  if (st == ENCA_TSTAT_EXECUTED)
    atomic_fetch_add (&s->st.executed, 1);
  if (st == ENCA_TSTAT_FAILED)
    atomic_fetch_add (&s->st.failed, 1);
  enca_mutex_unlock (&s->rlock);
}

static enca_sched_result *
sched_make_result (const enca_sched_task *t,
                   enca_timestamp_ns submitted_ns,
                   enca_timestamp_ns dispatched_ns,
                   enca_task_status status, enca_u64 value)
{
  enca_sched_result *r = enca_malloc (sizeof *r);
  ENCA_ASSERT_ALWAYS (r != NULL, "result alloc failed");
  memset (r, 0, sizeof *r);
  r->task_id = t->task_id;
  r->document_id = t->document_id;
  r->generation = t->generation;
  r->document_revision = t->document_revision;
  r->cls = t->cls;
  r->status = status;
  r->submitted_ns = submitted_ns;
  r->admitted_ns = submitted_ns;   /* admission is inline at submit */
  r->dispatched_ns = dispatched_ns;
  r->finished_ns = enca_monotonic_now_ns ();
  r->value = value;
  return r;
}

/* Result for a queued task that leaves WITHOUT executing
   (superseded / stale / expired / shutdown).  Caller holds qlock. */
static void
node_drop (enca_scheduler *s, enca_sched_node *n,
           enca_task_status status)
{
  if (n->task.release_snapshot)
    n->task.release_snapshot (n->task.snapshot_handle);
  /* qlock -> rlock is the only nesting order used anywhere. */
  sched_push_result (
    s, sched_make_result (&n->task, n->submitted_ns,
                          n->submitted_ns, status, 0));
  enca_free (n);
}

/* Supersession scan helpers: find any queued entry the INCOMING task
   supersedes (strictly older, same domain), and find any queued entry
   that would fold the incoming one (equal or newer revision). */
static enca_sched_node *
find_older (enca_task_queue *q, const enca_sched_task *in)
{
  for (enca_sched_node *n = q->head; n; n = n->next)
    if (n->task.document_id == in->document_id
        && n->task.document_revision < in->document_revision)
      return n;
  return NULL;
}

static bool
has_equal_or_newer (enca_task_queue *q, const enca_sched_task *in)
{
  for (enca_sched_node *n = q->head; n; n = n->next)
    if (n->task.document_id == in->document_id
        && n->task.document_revision >= in->document_revision)
      return true;
  return false;
}

enca_admit_result
enca_sched_submit (enca_scheduler *s, const enca_sched_task *in,
                   enca_u64 *out_id)
{
  if (!s || !in || in->cls >= ENCA_TCLASS_COUNT)
    return ENCA_ADMIT_REJECTED;

  atomic_fetch_add (&s->st.submitted, 1);

  /* Admission gate part 1: dead-on-arrival deadlines never queue. */
  if (!enca_deadline_is_none (in->deadline_ns)
      && enca_monotonic_now_ns () > in->deadline_ns.abs_ns)
    {
      atomic_fetch_add (&s->st.dropped_expired_submit, 1);
      return ENCA_ADMIT_DROPPED_EXPIRED;
    }

  enca_mutex_lock (&s->lock);
  enca_task_queue *q = &s->q[in->cls];
  bool evicted = false;

  /* INTERACTIVE / BACKGROUND use supersession-based REPLACE/FOLD.
     MAINTENANCE is FIFO ACCEPT; SYSTEM always accepts. */
  if (in->cls == ENCA_TCLASS_INTERACTIVE
      || in->cls == ENCA_TCLASS_BACKGROUND)
    {
      /* Fold: an equal-or-newer task is already queued for this
         domain -- the incoming one has no future value. */
      if (has_equal_or_newer (q, in))
        {
          atomic_fetch_add (&s->st.folded, 1);
          enca_mutex_unlock (&s->lock);
          return ENCA_ADMIT_FOLDED;
        }
      /* Replace: evict every strictly older same-domain entry. */
      enca_sched_node *old;
      while ((old = find_older (q, in)) != NULL)
        {
          queue_unlink (q, old);
          node_drop (s, old, ENCA_TSTAT_DROPPED_SUPERSEDED);
          atomic_fetch_add (&s->st.replaced, 1);
          evicted = true;
        }
    }

  enca_sched_node *n = enca_malloc (sizeof *n);
  if (!n)
    {
      enca_mutex_unlock (&s->lock);
      return ENCA_ADMIT_REJECTED;
    }
  n->task = *in;
  n->task.task_id = atomic_fetch_add (&s->next_task_id, 1);
  queue_push (q, n);
  atomic_fetch_add (&s->st.accepted, 1);
  atomic_store (&s->st.queued[in->cls], (enca_u64) q->count);

  enca_admit_result res
    = evicted ? ENCA_ADMIT_REPLACED : ENCA_ADMIT_ACCEPTED;

  if (out_id)
    *out_id = n->task.task_id;
  /* Signal while holding the lock: a worker that checked queues and
     found them empty either has not entered cond_wait yet (it will
     re-check under the lock) or is already waiting and gets woken. */
  enca_condition_signal (&s->wake);
  enca_mutex_unlock (&s->lock);
  return res;
}

bool
enca_sched_pop (enca_scheduler *s, enca_u64 now_ns,
                enca_u64 current_generation,
                enca_sched_rev_fn doc_rev_fn, void *rev_ctx,
                enca_sched_task *out, enca_drop_reason *why)
{
  if (!s)
    return false;

  static const int order[ENCA_TCLASS_COUNT]
    = { ENCA_TCLASS_SYSTEM, ENCA_TCLASS_INTERACTIVE,
        ENCA_TCLASS_BACKGROUND, ENCA_TCLASS_MAINTENANCE };

  enca_mutex_lock (&s->lock);
  bool got = false;
  enca_drop_reason last = ENCA_DROP_NONE;

  for (int oi = 0; oi < ENCA_TCLASS_COUNT && !got; oi++)
    {
      enca_task_queue *q = &s->q[order[oi]];
      enca_sched_node *n = q->head;
      while (n)
        {
          enca_drop_reason drop = ENCA_DROP_NONE;

          if (n->task.generation != current_generation)
            drop = ENCA_DROP_STALE;
          else if (doc_rev_fn
                   && doc_rev_fn (rev_ctx, n->task.document_id)
                        != n->task.document_revision)
            drop = ENCA_DROP_STALE;
          else if (!enca_deadline_is_none (n->task.deadline_ns)
                   && now_ns > n->task.deadline_ns.abs_ns)
            drop = ENCA_DROP_EXPIRED;

          if (drop != ENCA_DROP_NONE)
            {
              enca_sched_node *dead = n;
              n = n->next;
              queue_unlink (q, dead);
              node_drop (s, dead,
                (drop == ENCA_DROP_STALE) ? ENCA_TSTAT_DROPPED_STALE
                                          : ENCA_TSTAT_DROPPED_EXPIRED);
              if (drop == ENCA_DROP_STALE)
                atomic_fetch_add (&s->st.dropped_stale_dispatch, 1);
              else
                atomic_fetch_add (&s->st.dropped_expired_dispatch, 1);
              last = drop;
              continue;
            }

          /* Dispatchable. */
          enca_sched_node *taken = queue_unlink (q, n);
          *out = taken->task;
          enca_free (taken);
          if (why)
            *why = ENCA_DROP_NONE;
          got = true;
          break;
        }
    }
  for (int c = 0; c < ENCA_TCLASS_COUNT; c++)
    atomic_store (&s->st.queued[c], s->q[c].count);
  enca_mutex_unlock (&s->lock);

  if (!got && why && last != ENCA_DROP_NONE)
    *why = last;
  return got;
}

enca_usize
enca_sched_shutdown_drain (enca_scheduler *s)
{
  if (!s)
    return 0;
  enca_usize removed = 0;
  enca_mutex_lock (&s->lock);
  for (int c = 0; c < ENCA_TCLASS_COUNT; c++)
    {
      enca_task_queue *q = &s->q[c];
      while (q->head)
        {
          enca_sched_node *n = queue_unlink (q, q->head);
          node_drop (s, n, ENCA_TSTAT_DROPPED_SHUTDOWN);
          removed++;
        }
      atomic_store (&s->st.queued[c], 0);
    }
  enca_mutex_unlock (&s->lock);
  return removed;
}

void
enca_sched_destroy (enca_scheduler *s)
{
  if (!s)
    return;
  enca_usize left = enca_sched_shutdown_drain (s);
  ENCA_ASSERT_ALWAYS (left == 0, "scheduler destroyed with queued tasks");
  enca_condition_destroy (&s->wake);
  enca_mutex_destroy (&s->rlock);
  enca_mutex_destroy (&s->lock);
}

/* ---------------- P3.2: minimal executor ---------------- */

/* Take the next dispatchable task under qlock, running the SECOND
   admission gate (generation / revision / deadline) inline: tasks
   that cannot commit are dropped here with results, never executed. */
static bool
sched_take (enca_scheduler *s, enca_sched_task *out,
            enca_timestamp_ns *submitted_ns)
{
  static const int order[ENCA_TCLASS_COUNT]
    = { ENCA_TCLASS_SYSTEM, ENCA_TCLASS_INTERACTIVE,
        ENCA_TCLASS_BACKGROUND, ENCA_TCLASS_MAINTENANCE };
  enca_u64 gen = atomic_load (&s->cur_gen);
  enca_u64 now = enca_monotonic_now_ns ();

  for (int oi = 0; oi < ENCA_TCLASS_COUNT; oi++)
    {
      enca_task_queue *q = &s->q[order[oi]];
      enca_sched_node *n = q->head;
      while (n)
        {
          if (n->task.generation != gen)
            {
              enca_sched_node *d = n;
              n = n->next;
              queue_unlink (q, d);
              node_drop (s, d, ENCA_TSTAT_DROPPED_STALE);
              atomic_fetch_add (&s->st.dropped_stale_dispatch, 1);
              continue;
            }
          if (!enca_deadline_is_none (n->task.deadline_ns)
              && now > n->task.deadline_ns.abs_ns)
            {
              enca_sched_node *d = n;
              n = n->next;
              queue_unlink (q, d);
              node_drop (s, d, ENCA_TSTAT_DROPPED_EXPIRED);
              atomic_fetch_add (&s->st.dropped_expired_dispatch, 1);
              continue;
            }
          /* Dispatchable. */
          enca_sched_node *taken = queue_unlink (q, n);
          *out = taken->task;
          *submitted_ns = taken->submitted_ns;
          enca_free (taken);
          for (int c = 0; c < ENCA_TCLASS_COUNT; c++)
            atomic_store (&s->st.queued[c], s->q[c].count);
          return true;
        }
    }
  for (int c = 0; c < ENCA_TCLASS_COUNT; c++)
    atomic_store (&s->st.queued[c], s->q[c].count);
  return false;
}

static enca_result
worker_main (void *arg)
{
  enca_scheduler *s = arg;

  enca_mutex_lock (&s->lock);
  for (;;)
    {
      enca_sched_task t;
      enca_timestamp_ns submitted_ns;

      if (sched_take (s, &t, &submitted_ns))
        {
          /* Execute OUTSIDE the queue lock. */
          bool stopping = atomic_load ((atomic_int *)&s->state)
                          != ENCA_SCHED_RUNNING;
          enca_mutex_unlock (&s->lock);

          enca_timestamp_ns dispatched = enca_monotonic_now_ns ();
          enca_u64 value = 0;
          if (stopping)
            {
              /* Shutdown policy v1: queued work is dropped, never
                 executed. */
              enca_sched_result *r
                = sched_make_result (&t, submitted_ns, dispatched,
                                     ENCA_TSTAT_DROPPED_SHUTDOWN, 0);
              sched_push_result (s, r);
            }
          else
            {
              enca_task_status status = ENCA_TSTAT_EXECUTED;
              if (s->exec_fn == NULL
                  || s->exec_fn (&t, s->exec_ctx, &value) != 0)
                status = ENCA_TSTAT_FAILED;
              enca_sched_result *r
                = sched_make_result (&t, submitted_ns, dispatched,
                                     status, value);
              sched_push_result (s, r);
            }

          enca_mutex_lock (&s->lock);
          continue;
        }

      /* Nothing dispatchable. */
      if (atomic_load ((atomic_int *)&s->state) != ENCA_SCHED_RUNNING)
        break;                    /* shutting down and fully drained */

      enca_condition_wait (&s->wake, &s->lock);
      /* Loop re-evaluates queues and state under the lock. */
    }
  enca_mutex_unlock (&s->lock);
  return ENCA_OK;
}

enca_result
enca_sched_start_workers (enca_scheduler *s, unsigned n_workers,
                          enca_task_execute_fn exec_fn, void *exec_ctx)
{
  if (!s || n_workers == 0)
    return ENCA_ERR_INVALID_ARGUMENT;

  s->workers = enca_malloc (n_workers * sizeof (enca_thread));
  if (!s->workers)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (s->workers, 0, n_workers * sizeof (enca_thread));

  s->exec_fn = exec_fn;
  s->exec_ctx = exec_ctx;
  s->n_workers = n_workers;
  for (unsigned i = 0; i < n_workers; i++)
    {
      char name[32];
      snprintf (name, sizeof name, "enca-sched-%u", i);
      enca_result r = enca_thread_create (&s->workers[i], name,
                                          worker_main, s);
      if (ENCA_RESULT_IS_ERR (r))
        {
          s->n_workers = i;
          return r;
        }
    }
  return ENCA_OK;
}

void
enca_sched_advance_generation (enca_scheduler *s)
{
  /* Hold the queue lock while bumping and broadcasting: a worker
     checking queues under the same lock either sees the new epoch or
     gets woken by the broadcast -- no lost-wakeup window. */
  enca_mutex_lock (&s->lock);
  atomic_fetch_add (&s->cur_gen, 1);
  enca_condition_broadcast (&s->wake);
  enca_mutex_unlock (&s->lock);
}

enca_u64
enca_sched_current_generation (const enca_scheduler *s)
{
  return atomic_load (&s->cur_gen);
}

enca_sched_state_enum
enca_sched_get_state (const enca_scheduler *s)
{
  return (enca_sched_state_enum) atomic_load (
    (const atomic_int *) &s->state);
}

enca_usize
enca_sched_poll (enca_scheduler *s, enca_sched_commit_fn commit_cb,
                 void *ctx)
{
  if (!s)
    return 0;
  enca_usize n = 0;
  for (;;)
    {
      enca_mutex_lock (&s->rlock);
      enca_sched_result *r = s->res_head;
      if (!r)
        {
          enca_mutex_unlock (&s->rlock);
          break;
        }
      s->res_head = r->next;
      if (!s->res_head)
        s->res_tail = NULL;
      s->res_count--;
      enca_mutex_unlock (&s->rlock);

      /* Commit decisions belong to the integration boundary, not the
         worker (#7 of P3.2). */
      if (commit_cb)
        commit_cb (r, ctx);
      enca_free (r);
      n++;
    }
  return n;
}

void
enca_sched_shutdown (enca_scheduler *s)
{
  if (!s)
    return;

  /* STOP_ACCEPTING under the queue lock: a worker about to enter
     cond_wait either sees the new state or gets woken -- no lost
     broadcast. */
  enca_mutex_lock (&s->lock);
  atomic_store ((atomic_int *) &s->state, ENCA_SCHED_STOP_ACCEPTING);
  enca_condition_broadcast (&s->wake);
  enca_mutex_unlock (&s->lock);

  /* Workers drain remaining queued tasks as DROPPED_SHUTDOWN results,
     then exit once the queues are empty. */
  for (unsigned i = 0; i < s->n_workers; i++)
    enca_thread_join (&s->workers[i]);

  atomic_store ((atomic_int *) &s->state, ENCA_SCHED_JOINED);

  enca_free (s->workers);
  s->workers = NULL;
  s->n_workers = 0;

  /* Any straggler would violate the accounting identity. */
  ENCA_ASSERT_ALWAYS (enca_sched_shutdown_drain (s) == 0,
                      "scheduler drained after join: unexpected tasks");
}

