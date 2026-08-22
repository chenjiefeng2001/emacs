#ifndef ENCA_BLOCKING_QUEUE_H
#define ENCA_BLOCKING_QUEUE_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../event/event.h"
#include "../thread/thread.h"
#include "../time/time.h"

typedef struct enca_blocking_queue
{
  enca_mutex lock;
  enca_condition not_empty;
  enca_condition not_full;
  enca_event *items;
  enca_u32 capacity;
  enca_u32 head;
  enca_u32 count;
  bool closed;
} enca_blocking_queue;

enca_result enca_bq_init (enca_blocking_queue *q, enca_u32 capacity);
void enca_bq_destroy (enca_blocking_queue *q);

enca_result enca_bq_push (enca_blocking_queue *q, const enca_event *e,
                         enca_deadline deadline);
enca_result enca_bq_pop (enca_blocking_queue *q, enca_event *out,
                        enca_deadline deadline);

enca_result enca_bq_try_push (enca_blocking_queue *q, const enca_event *e);
enca_result enca_bq_try_pop (enca_blocking_queue *q, enca_event *out);

void enca_bq_close (enca_blocking_queue *q);

ENCA_INLINE enca_usize
enca_bq_size (enca_blocking_queue *q)
{
  return q ? q->count : 0;
}

#endif
