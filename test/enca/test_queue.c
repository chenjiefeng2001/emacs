#include "test_util.h"
#include "../../src/enca/queue/spsc_ring.h"
#include "../../src/enca/queue/blocking_queue.h"

#include <stdatomic.h>

static void
test_spsc_fifo_single_thread (void)
{
  enca_spsc_ring r;
  CHECK_EQ_U64 (enca_spsc_init (&r, 8), ENCA_OK);
  CHECK_EQ_U64 (r.capacity, 8);

  enca_event e;
  for (int i = 0; i < 8; i++)
    {
      enca_event_init (&e, ENCA_EVENT_TIMER, (enca_object_id) i, 0, NULL);
      CHECK (enca_spsc_try_push (&r, &e));
    }
  CHECK (!enca_spsc_try_push (&r, &e));
  CHECK_EQ_U64 (enca_spsc_size (&r), 8);

  for (int i = 0; i < 8; i++)
    {
      CHECK (enca_spsc_try_pop (&r, &e));
      CHECK_EQ_U64 (e.source, (enca_object_id) i);
    }
  CHECK (!enca_spsc_try_pop (&r, &e));
  CHECK_EQ_U64 (enca_spsc_size (&r), 0);

  enca_spsc_destroy (&r);
}

static enca_spsc_ring stress_ring;
static _Atomic enca_u64 stress_received;
static _Atomic enca_seq_t stress_last_seq;
static _Atomic bool stress_order_ok;

#define STRESS_N 200000

static enca_result
spsc_producer (void *arg)
{
  (void) arg;
  enca_event e;

  for (int i = 0; i < STRESS_N; i++)
    {
      enca_event_init (&e, ENCA_EVENT_RUNTIME, 777, 0, NULL);
      while (!enca_spsc_try_push (&stress_ring, &e))
        enca_thread_yield ();
    }
  return ENCA_OK;
}

static enca_result
spsc_consumer (void *arg)
{
  (void) arg;
  enca_event e;

  for (;;)
    {
      if (enca_spsc_try_pop (&stress_ring, &e))
        {
          enca_seq_t last
            = atomic_load_explicit (&stress_last_seq, memory_order_relaxed);
          if (e.sequence <= last)
            atomic_store_explicit (&stress_order_ok, false,
                                   memory_order_relaxed);
          atomic_store_explicit (&stress_last_seq, e.sequence,
                                 memory_order_relaxed);
          atomic_fetch_add_explicit (&stress_received, 1,
                                     memory_order_relaxed);
          continue;
        }

      if (atomic_load_explicit (&stress_received,
                                memory_order_acquire) >= STRESS_N)
        break;
      enca_thread_yield ();
    }
  return ENCA_OK;
}

static void
test_spsc_multithreaded_stress (void)
{
  CHECK_EQ_U64 (enca_spsc_init (&stress_ring, 1024), ENCA_OK);
  atomic_store (&stress_received, 0);
  atomic_store (&stress_last_seq, 0);
  atomic_store (&stress_order_ok, true);

  enca_thread prod, cons;
  CHECK_EQ_U64 (enca_thread_create (&prod, "prod", spsc_producer, NULL),
                ENCA_OK);
  CHECK_EQ_U64 (enca_thread_create (&cons, "cons", spsc_consumer, NULL),
                ENCA_OK);
  enca_thread_join (&cons);
  enca_thread_join (&prod);

  CHECK_EQ_U64 (atomic_load (&stress_received), STRESS_N);
  CHECK (atomic_load (&stress_order_ok));

  enca_spsc_destroy (&stress_ring);
}

static void
test_bq_timeout_and_close (void)
{
  enca_blocking_queue q;
  CHECK_EQ_U64 (enca_bq_init (&q, 2), ENCA_OK);

  enca_event e = { 0 };
  e.type = ENCA_EVENT_PROCESS;

  CHECK_EQ_U64 (enca_bq_try_push (&q, &e), ENCA_OK);
  CHECK_EQ_U64 (enca_bq_try_push (&q, &e), ENCA_OK);
  CHECK_EQ_U64 (enca_bq_try_push (&q, &e), ENCA_ERR_WOULD_BLOCK);

  enca_deadline fast = enca_deadline_from_now_ms (5);
  CHECK_EQ_U64 (enca_bq_push (&q, &e, fast), ENCA_ERR_TIMEOUT);

  CHECK_EQ_U64 (enca_bq_try_pop (&q, &e), ENCA_OK);
  CHECK_EQ_U64 (enca_bq_try_pop (&q, &e), ENCA_OK);
  CHECK_EQ_U64 (enca_bq_try_pop (&q, &e), ENCA_ERR_WOULD_BLOCK);

  enca_deadline also_fast = enca_deadline_from_now_ms (5);
  CHECK_EQ_U64 (enca_bq_pop (&q, &e, also_fast), ENCA_ERR_TIMEOUT);

  enca_bq_close (&q);
  CHECK_EQ_U64 (enca_bq_try_pop (&q, &e), ENCA_ERR_CLOSED);
  CHECK_EQ_U64 (enca_bq_pop (&q, &e, ENCA_DEADLINE_NONE), ENCA_ERR_CLOSED);
  CHECK_EQ_U64 (enca_bq_try_push (&q, &e), ENCA_ERR_CLOSED);
  CHECK_EQ_U64 (enca_bq_push (&q, &e, ENCA_DEADLINE_NONE), ENCA_ERR_CLOSED);

  enca_bq_destroy (&q);
}

static enca_blocking_queue bq_stress_q;
static _Atomic enca_u64 bq_pushed, bq_popped;
static _Atomic int bq_consumer_exit_r;

enum { BQ_PRODUCERS = 4, BQ_PER_PRODUCER = 25000 };

static enca_result
bq_producer (void *arg)
{
  (void) arg;
  enca_event e;

  for (int i = 0; i < BQ_PER_PRODUCER; i++)
    {
      enca_event_init (&e, ENCA_EVENT_BUFFER_CHANGED, 1, 0, NULL);
      while (enca_bq_try_push (&bq_stress_q, &e) != ENCA_OK)
        enca_thread_yield ();
      atomic_fetch_add_explicit (&bq_pushed, 1, memory_order_relaxed);
    }
  return ENCA_OK;
}

static enca_result
bq_consumer (void *arg)
{
  (void) arg;
  enca_event e;
  enca_deadline dl = enca_deadline_from_now_ms (60000);

  for (;;)
    {
      enca_result r = enca_bq_pop (&bq_stress_q, &e, dl);
      if (r == ENCA_OK)
        {
          atomic_fetch_add_explicit (&bq_popped, 1, memory_order_relaxed);
          continue;
        }
      atomic_store_explicit (&bq_consumer_exit_r, (int) r,
                            memory_order_relaxed);
      break;
    }
  return ENCA_OK;
}

static void
test_bq_multi_producer_consumer (void)
{
  CHECK_EQ_U64 (enca_bq_init (&bq_stress_q, 256), ENCA_OK);
  atomic_store (&bq_pushed, 0);
  atomic_store (&bq_popped, 0);

  enca_thread producers[BQ_PRODUCERS];
  enca_thread consumer;

  CHECK_EQ_U64 (
    enca_thread_create (&consumer, "bq-cons", bq_consumer, NULL), ENCA_OK);
  for (int i = 0; i < BQ_PRODUCERS; i++)
    CHECK_EQ_U64 (
      enca_thread_create (&producers[i], "bq-prod", bq_producer, NULL),
      ENCA_OK);

  for (int i = 0; i < BQ_PRODUCERS; i++)
    enca_thread_join (&producers[i]);

  enca_bq_close (&bq_stress_q);
  enca_thread_join (&consumer);

  CHECK_EQ_U64 (atomic_load (&bq_pushed), BQ_PRODUCERS * BQ_PER_PRODUCER);
  CHECK_EQ_U64 (atomic_load (&bq_popped), BQ_PRODUCERS * BQ_PER_PRODUCER);

  enca_bq_destroy (&bq_stress_q);
}

static void
test_bq_invalid_init (void)
{
  enca_blocking_queue q;
  CHECK (ENCA_RESULT_IS_ERR (enca_bq_init (&q, 0)));
  CHECK (ENCA_RESULT_IS_ERR (enca_bq_init (NULL, 4)));
}

void
run_test_queue (void)
{
  enca_test_run_suite ("queue/spsc-fifo", test_spsc_fifo_single_thread);
  enca_test_run_suite ("queue/spsc-stress", test_spsc_multithreaded_stress);
  enca_test_run_suite ("queue/bq-timeout-close",
                       test_bq_timeout_and_close);
  enca_test_run_suite ("queue/bq-stress", test_bq_multi_producer_consumer);
  enca_test_run_suite ("queue/bq-invalid", test_bq_invalid_init);
}
