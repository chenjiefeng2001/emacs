#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "blocking_queue.h"

#include "../base/assert.h"
#include <stdlib.h>
#include <string.h>

enca_result
enca_bq_init (enca_blocking_queue *q, enca_u32 capacity)
{
  if (!q || capacity == 0)
    return ENCA_ERR_INVALID_ARGUMENT;

  memset (q, 0, sizeof *q);
  enca_result r = enca_mutex_init (&q->lock);
  if (ENCA_RESULT_IS_ERR (r))
    return r;
  r = enca_condition_init (&q->not_empty);
  if (ENCA_RESULT_IS_ERR (r))
    goto fail_cond;
  r = enca_condition_init (&q->not_full);
  if (ENCA_RESULT_IS_ERR (r))
    goto fail_cond2;

  q->items = calloc (capacity, sizeof (enca_event));
  if (!q->items)
    {
      r = ENCA_ERR_OUT_OF_MEMORY;
      goto fail_items;
    }
  q->capacity = capacity;
  return ENCA_OK;

fail_items:
  enca_condition_destroy (&q->not_full);
fail_cond2:
  enca_condition_destroy (&q->not_empty);
fail_cond:
  enca_mutex_destroy (&q->lock);
  memset (q, 0, sizeof *q);
  return r;
}

void
enca_bq_destroy (enca_blocking_queue *q)
{
  if (!q)
    return;
  free (q->items);
  enca_condition_destroy (&q->not_full);
  enca_condition_destroy (&q->not_empty);
  enca_mutex_destroy (&q->lock);
  memset (q, 0, sizeof *q);
}

void
enca_bq_close (enca_blocking_queue *q)
{
  ENCA_ASSERT (q != NULL, "null queue");
  enca_mutex_lock (&q->lock);
  q->closed = true;
  enca_condition_broadcast (&q->not_empty);
  enca_condition_broadcast (&q->not_full);
  enca_mutex_unlock (&q->lock);
}

enca_result
enca_bq_push (enca_blocking_queue *q, const enca_event *e,
             enca_deadline deadline)
{
  ENCA_ASSERT (q != NULL && e != NULL, "null queue or event");

  enca_mutex_lock (&q->lock);
  for (;;)
    {
      if (q->closed)
        {
          enca_mutex_unlock (&q->lock);
          return ENCA_ERR_CLOSED;
        }

      if (q->count < q->capacity)
        {
          enca_u32 slot = (q->head + q->count) % q->capacity;
          q->items[slot] = *e;
          q->count++;
          enca_condition_signal (&q->not_empty);
          enca_mutex_unlock (&q->lock);
          return ENCA_OK;
        }

      enca_u64 remain_ns = enca_deadline_remaining_ns (deadline);
      if (remain_ns == 0)
        {
          enca_mutex_unlock (&q->lock);
          return ENCA_ERR_TIMEOUT;
        }

      bool woke = enca_condition_timed_wait (&q->not_full, &q->lock,
                                             remain_ns);
      (void) woke;
    }
}

enca_result
enca_bq_pop (enca_blocking_queue *q, enca_event *out, enca_deadline deadline)
{
  ENCA_ASSERT (q != NULL && out != NULL, "null queue or out");

  enca_mutex_lock (&q->lock);
  for (;;)
    {
      ENCA_ASSERT_ALWAYS (q->head < q->capacity, "bq head out of range");
      ENCA_ASSERT_ALWAYS (q->count <= q->capacity, "bq count overflow");

      if (q->count > 0)
        {
          *out = q->items[q->head];
          q->head = (q->head + 1) % q->capacity;
          q->count--;
          enca_condition_signal (&q->not_full);
          enca_mutex_unlock (&q->lock);
          return ENCA_OK;
        }

      if (q->closed)
        {
          enca_mutex_unlock (&q->lock);
          return ENCA_ERR_CLOSED;
        }

      enca_u64 remain_ns = enca_deadline_remaining_ns (deadline);
      if (remain_ns == 0)
        {
          enca_mutex_unlock (&q->lock);
          return ENCA_ERR_TIMEOUT;
        }

      enca_condition_timed_wait (&q->not_empty, &q->lock, remain_ns);
    }
}

enca_result
enca_bq_try_push (enca_blocking_queue *q, const enca_event *e)
{
  ENCA_ASSERT (q != NULL && e != NULL, "null queue or event");

  enca_result res = ENCA_ERR_WOULD_BLOCK;
  enca_mutex_lock (&q->lock);

  if (q->closed)
    res = ENCA_ERR_CLOSED;
  else if (q->count < q->capacity)
    {
      enca_u32 slot = (q->head + q->count) % q->capacity;
      q->items[slot] = *e;
      q->count++;
      enca_condition_signal (&q->not_empty);
      res = ENCA_OK;
    }

  enca_mutex_unlock (&q->lock);
  return res;
}

enca_result
enca_bq_try_pop (enca_blocking_queue *q, enca_event *out)
{
  ENCA_ASSERT (q != NULL && out != NULL, "null queue or out");

  enca_result res = ENCA_ERR_WOULD_BLOCK;
  enca_mutex_lock (&q->lock);

  if (q->count > 0)
    {
      *out = q->items[q->head];
      q->head = (q->head + 1) % q->capacity;
      q->count--;
      enca_condition_signal (&q->not_full);
      res = ENCA_OK;
    }
  else if (q->closed)
    res = ENCA_ERR_CLOSED;

  enca_mutex_unlock (&q->lock);
  return res;
}
