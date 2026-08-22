#include "trace.h"

#include "../thread/thread.h"
#include "../time/time.h"
#include <stdatomic.h>
#include <string.h>

static _Atomic bool trace_on;
static _Atomic enca_u64 trace_write_idx;
static enca_trace_record trace_records[ENCA_TRACE_CAPACITY];

void
enca_trace_set_enabled (bool enabled)
{
  atomic_store_explicit (&trace_on, enabled, memory_order_release);
}

bool
enca_trace_enabled (void)
{
  return atomic_load_explicit (&trace_on, memory_order_acquire);
}

void
enca_trace_emit (enca_trace_kind kind, enca_u64 id, enca_u64 data)
{
  if (!enca_trace_enabled ())
    return;

  enca_u64 idx = atomic_fetch_add_explicit (&trace_write_idx, 1,
                                            memory_order_relaxed);
  enca_trace_record *r = &trace_records[idx % ENCA_TRACE_CAPACITY];
  r->ts_ns = enca_monotonic_now_ns ();
  r->tid = enca_thread_self_id ();
  r->kind = kind;
  r->id = id;
  r->data = data;
}

enca_usize
enca_trace_record_count (void)
{
  enca_u64 n = atomic_load_explicit (&trace_write_idx, memory_order_acquire);
  if (n > ENCA_TRACE_CAPACITY)
    return ENCA_TRACE_CAPACITY;
  return (enca_usize) n;
}

const char *
enca_trace_kind_str (enca_trace_kind kind)
{
  switch (kind)
    {
    case ENCA_TRACE_EVENT_PUBLISH:
      return "event-publish";
    case ENCA_TRACE_EVENT_DISPATCH:
      return "event-dispatch";
    case ENCA_TRACE_TASK_BEGIN:
      return "task-begin";
    case ENCA_TRACE_TASK_END:
      return "task-end";
    case ENCA_TRACE_QUEUE_PUSH:
      return "queue-push";
    case ENCA_TRACE_QUEUE_POP:
      return "queue-pop";
    case ENCA_TRACE_ALLOC:
      return "alloc";
    case ENCA_TRACE_FREE:
      return "free";
    case ENCA_TRACE_CUSTOM:
      return "custom";
    }
  return "unknown";
}

bool
enca_trace_dump_chrome (FILE *out)
{
  if (!out)
    return false;

  enca_usize count = enca_trace_record_count ();
  enca_usize start = 0;
  enca_u64 total
    = atomic_load_explicit (&trace_write_idx, memory_order_acquire);
  if (total > ENCA_TRACE_CAPACITY)
    start = (enca_usize) (total - ENCA_TRACE_CAPACITY);

  fputs ("{\"traceEvents\":[", out);

  for (enca_usize i = 0; i < count; i++)
    {
      const enca_trace_record *r
        = &trace_records[(start + i) % ENCA_TRACE_CAPACITY];

      if (i > 0)
        fputc (',', out);

      fprintf (out,
               "{\"ph\":\"i\",\"cat\":\"enca\",\"ts\":" ENCA_U64F
               ".%03u,\"pid\":0,\"tid\":%u,\"name\":\"%s\",\"args\":{\"id\":"
               ENCA_U64F ",\"data\":" ENCA_U64F "}}",
               (unsigned long long) (r->ts_ns / 1000),
               (unsigned) ((r->ts_ns % 1000) / 1), r->tid,
               enca_trace_kind_str (r->kind), (unsigned long long) r->id,
               (unsigned long long) r->data);
    }

  fputs ("]}\n", out);
  return true;
}
