#include "scheduler.h"

#include "../base/assert.h"
#include "../memory/memory.h"

#include <string.h>

enca_result
enca_sched_init (enca_scheduler *s)
{
  if (!s)
    return ENCA_ERR_INVALID_ARGUMENT;
  memset (s, 0, sizeof *s);
  enca_result r = enca_mutex_init (&s->lock);
  if (ENCA_RESULT_IS_ERR (r))
    return r;
  atomic_store (&s->next_task_id, 1);
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

static void
node_drop (enca_sched_node *n)
{
  if (n->task.release_snapshot)
    n->task.release_snapshot (n->task.snapshot_handle);
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
          node_drop (old);
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
              node_drop (dead);
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
          node_drop (n);
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
  enca_mutex_destroy (&s->lock);
}
