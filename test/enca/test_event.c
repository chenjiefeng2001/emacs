#include "test_util.h"
#include "../../src/enca/event/event.h"
#include "../../src/enca/event/dispatch.h"
#include "../../src/enca/queue/spsc_ring.h"
#include "../../src/enca/queue/blocking_queue.h"
#include "../../src/enca/id/id.h"

#include <stdatomic.h>

static void
test_sequence_increases (void)
{
  enca_seq_t s1 = enca_event_next_sequence ();
  enca_seq_t s2 = enca_event_next_sequence ();
  enca_seq_t s3 = enca_event_next_sequence ();

  CHECK (s1 < s2);
  CHECK (s2 < s3);
}

static void
test_event_init_fields (void)
{
  int payload;
  enca_object_id src = enca_id_make (ENCA_OBJ_BUFFER, 7, 42);

  enca_event e;
  enca_event_init (&e, ENCA_EVENT_BUFFER_CHANGED, src, 0x1234, &payload);

  CHECK_EQ_U64 (e.type, ENCA_EVENT_BUFFER_CHANGED);
  CHECK_EQ_U64 (e.source, src);
  CHECK_EQ_U64 (e.flags, 0x1234);
  CHECK (e.payload == &payload);
  CHECK (e.sequence != 0);
  CHECK (e.timestamp != 0);
  CHECK (enca_event_type_str (e.type) != NULL);
  CHECK (enca_event_type_str ((enca_event_type) 77) != NULL);
}

static _Atomic int handler_calls;

static enca_result
counting_handler (const enca_event *e, void *ctx)
{
  (void) e;
  atomic_fetch_add_explicit (&handler_calls, 1, memory_order_relaxed);
  return *(enca_result *) ctx;
}

static void
test_dispatch_spsc_drains (void)
{
  enca_spsc_ring r;
  enca_spsc_init (&r, 16);

  enca_result handler_rc = ENCA_OK;
  atomic_store (&handler_calls, 0);

  for (int i = 0; i < 5; i++)
    {
      enca_event e;
      enca_event_init (&e, ENCA_EVENT_TIMER, i, 0, NULL);
      CHECK (enca_spsc_try_push (&r, &e));
    }

  enca_usize n = enca_event_dispatch_spsc (&r, counting_handler,
                                           &handler_rc, 100);
  CHECK_EQ_U64 (n, 5);
  CHECK_EQ_U64 (atomic_load (&handler_calls), 5);
  CHECK_EQ_U64 (enca_spsc_size (&r), 0);

  n = enca_event_dispatch_spsc (&r, counting_handler, &handler_rc, 100);
  CHECK_EQ_U64 (n, 0);

  enca_spsc_destroy (&r);
}

static void
test_dispatch_max_limit (void)
{
  enca_spsc_ring r;
  enca_spsc_init (&r, 16);

  enca_result handler_rc = ENCA_OK;
  atomic_store (&handler_calls, 0);

  for (int i = 0; i < 10; i++)
    {
      enca_event e;
      enca_event_init (&e, ENCA_EVENT_RUNTIME, i, 0, NULL);
      enca_spsc_try_push (&r, &e);
    }

  enca_usize n = enca_event_dispatch_spsc (&r, counting_handler,
                                           &handler_rc, 4);
  CHECK_EQ_U64 (n, 4);
  CHECK_EQ_U64 (atomic_load (&handler_calls), 4);
  CHECK_EQ_U64 (enca_spsc_size (&r), 6);

  enca_spsc_destroy (&r);
}

static enca_result
failing_handler (const enca_event *e, void *ctx)
{
  (void) e;
  (void) ctx;
  return ENCA_ERR_INTERNAL;
}

static void
test_dispatch_error_propagates (void)
{
  enca_blocking_queue q;
  enca_bq_init (&q, 8);

  enca_event e;
  enca_event_init (&e, ENCA_EVENT_PROCESS, 1, 0, NULL);
  enca_bq_try_push (&q, &e);

  enca_deadline dl = enca_deadline_from_now_ms (50);
  enca_result r = enca_event_dispatch_blocking (&q, failing_handler, NULL, dl);
  CHECK_EQ_U64 (r, ENCA_ERR_INTERNAL);

  enca_bq_close (&q);
  enca_bq_destroy (&q);
}

static void
test_dispatch_blocking_timeout_returns_ok (void)
{
  enca_blocking_queue q;
  enca_bq_init (&q, 8);

  enca_result handler_rc = ENCA_OK;
  atomic_store (&handler_calls, 0);

  enca_deadline dl = enca_deadline_from_now_ms (10);
  enca_result r
    = enca_event_dispatch_blocking (&q, counting_handler, &handler_rc, dl);
  CHECK_EQ_U64 (r, ENCA_OK);
  CHECK_EQ_U64 (atomic_load (&handler_calls), 0);

  enca_bq_close (&q);
  enca_bq_destroy (&q);
}

void
run_test_event (void)
{
  enca_test_run_suite ("event/sequence", test_sequence_increases);
  enca_test_run_suite ("event/init", test_event_init_fields);
  enca_test_run_suite ("event/dispatch-spsc", test_dispatch_spsc_drains);
  enca_test_run_suite ("event/dispatch-limit", test_dispatch_max_limit);
  enca_test_run_suite ("event/dispatch-error",
                       test_dispatch_error_propagates);
  enca_test_run_suite ("event/dispatch-timeout",
                       test_dispatch_blocking_timeout_returns_ok);
}
