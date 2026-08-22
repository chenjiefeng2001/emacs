#include "test_util.h"
#include "../../src/enca/thread/thread.h"
#include "../../src/enca/time/time.h"

#include <stdatomic.h>

static _Atomic int shared_counter;

static enca_result
increment_worker (void *arg)
{
  (void) arg;
  for (int i = 0; i < 10000; i++)
    atomic_fetch_add_explicit (&shared_counter, 1, memory_order_relaxed);
  return ENCA_OK;
}

static void
test_thread_create_join (void)
{
  atomic_store (&shared_counter, 0);

  enca_thread t;
  CHECK_EQ_U64 (enca_thread_create (&t, "test-inc", increment_worker, NULL),
                ENCA_OK);
  CHECK_EQ_U64 (enca_thread_join (&t), ENCA_OK);
  CHECK_EQ_U64 (atomic_load (&shared_counter), 10000);
  CHECK (t.native == NULL);
}

static void
test_many_threads_mutex_free_counter (void)
{
  enum { NT = 8 };
  atomic_store (&shared_counter, 0);

  enca_thread threads[NT];
  for (int i = 0; i < NT; i++)
    CHECK_EQ_U64 (
      enca_thread_create (&threads[i], "test-multi", increment_worker, NULL),
      ENCA_OK);

  for (int i = 0; i < NT; i++)
    CHECK_EQ_U64 (enca_thread_join (&threads[i]), ENCA_OK);

  CHECK_EQ_U64 (atomic_load (&shared_counter), NT * 10000);
}

static enca_mutex test_lock;
static long unprotected_long_counter;

static enca_result
mutex_worker (void *arg)
{
  (void) arg;
  for (int i = 0; i < 20000; i++)
    {
      enca_mutex_lock (&test_lock);
      unprotected_long_counter++;
      enca_mutex_unlock (&test_lock);
    }
  return ENCA_OK;
}

static void
test_mutex_protects (void)
{
  CHECK_EQ_U64 (enca_mutex_init (&test_lock), ENCA_OK);
  unprotected_long_counter = 0;

  enum { NT = 4 };
  enca_thread threads[NT];
  for (int i = 0; i < NT; i++)
    CHECK_EQ_U64 (
      enca_thread_create (&threads[i], "test-mutex", mutex_worker, NULL),
      ENCA_OK);
  for (int i = 0; i < NT; i++)
    enca_thread_join (&threads[i]);

  CHECK_EQ_U64 ((unsigned long long) unprotected_long_counter, NT * 20000);
  enca_mutex_destroy (&test_lock);
}

static enca_mutex hs_lock;
static enca_condition hs_cond;
static _Atomic bool hs_ready;

static enca_result
signaler (void *arg)
{
  (void) arg;
  enca_mutex_lock (&hs_lock);
  atomic_store_explicit (&hs_ready, true, memory_order_release);
  enca_condition_signal (&hs_cond);
  enca_mutex_unlock (&hs_lock);
  return ENCA_OK;
}

static void
test_condition_handshake (void)
{
  CHECK_EQ_U64 (enca_mutex_init (&hs_lock), ENCA_OK);
  CHECK_EQ_U64 (enca_condition_init (&hs_cond), ENCA_OK);
  atomic_store (&hs_ready, false);

  enca_thread t;
  CHECK_EQ_U64 (enca_thread_create (&t, "test-signal", signaler, NULL),
                ENCA_OK);

  enca_mutex_lock (&hs_lock);
  while (!atomic_load_explicit (&hs_ready, memory_order_acquire))
    enca_condition_wait (&hs_cond, &hs_lock);
  enca_mutex_unlock (&hs_lock);

  CHECK_EQ_U64 (enca_thread_join (&t), ENCA_OK);
  enca_condition_destroy (&hs_cond);
  enca_mutex_destroy (&hs_lock);
}

static void
test_timed_wait_times_out (void)
{
  enca_mutex m;
  enca_condition c;
  CHECK_EQ_U64 (enca_mutex_init (&m), ENCA_OK);
  CHECK_EQ_U64 (enca_condition_init (&c), ENCA_OK);

  enca_mutex_lock (&m);
  bool woke = enca_condition_timed_wait (&c, &m, 5 * ENCA_NS_PER_MS);
  enca_mutex_unlock (&m);
  CHECK (!woke);

  enca_condition_destroy (&c);
  enca_mutex_destroy (&m);
}

static void
test_try_lock (void)
{
  enca_mutex m;
  enca_mutex_init (&m);

  CHECK (enca_mutex_try_lock (&m));
  enca_mutex_unlock (&m);

  enca_mutex_lock (&m);
  CHECK (enca_mutex_try_lock (&m));
  CHECK (enca_mutex_try_lock (&m));
  enca_mutex_unlock (&m);
  enca_mutex_unlock (&m);
  enca_mutex_unlock (&m);

  CHECK (enca_mutex_try_lock (&m));
  enca_mutex_unlock (&m);
  enca_mutex_destroy (&m);
}

static unsigned slot_a, slot_b;
static _Atomic int tls_failures;

static enca_result
tls_worker (void *arg)
{
  (void) arg;

  static _Atomic unsigned per_thread_seq;
  unsigned v = atomic_fetch_add (&per_thread_seq, 1) + 100;

  enca_tls_set (slot_a, (void *) (enca_uptr) v);
  enca_tls_set (slot_b, (void *) (enca_uptr) (v * 2));

  for (int i = 0; i < 50; i++)
    enca_thread_yield ();

  if ((enca_uptr) enca_tls_get (slot_a) != v)
    atomic_fetch_add (&tls_failures, 1);
  if ((enca_uptr) enca_tls_get (slot_b) != (enca_uptr) (v * 2))
    atomic_fetch_add (&tls_failures, 1);
  return ENCA_OK;
}

static void
test_tls_slots (void)
{
  CHECK_EQ_U64 (enca_tls_alloc (&slot_a), ENCA_OK);
  CHECK_EQ_U64 (enca_tls_alloc (&slot_b), ENCA_OK);
  CHECK (slot_a != slot_b);

  atomic_store (&tls_failures, 0);

  enum { NT = 4 };
  enca_thread threads[NT];
  for (int i = 0; i < NT; i++)
    CHECK_EQ_U64 (
      enca_thread_create (&threads[i], "test-tls", tls_worker, NULL),
      ENCA_OK);
  for (int i = 0; i < NT; i++)
    enca_thread_join (&threads[i]);

  CHECK_EQ_U64 (atomic_load (&tls_failures), 0);

  enca_tls_free (slot_a);
  enca_tls_free (slot_b);

  unsigned recycled = 9999;
  CHECK_EQ_U64 (enca_tls_alloc (&recycled), ENCA_OK);
  CHECK (recycled == slot_a || recycled == slot_b);
  enca_tls_free (recycled);

  CHECK (ENCA_RESULT_IS_ERR (enca_tls_alloc (NULL)));
  CHECK (ENCA_RESULT_IS_ERR (enca_tls_free (ENCA_TLS_MAX_SLOTS + 5)));
}

static _Atomic enca_u32 main_tid_seen;

static enca_result
tid_worker (void *arg)
{
  (void) arg;
  enca_u32 tid = enca_thread_self_id ();
  atomic_store (&main_tid_seen, tid);
  CHECK (tid != 0);
  return ENCA_OK;
}

static void
test_self_id (void)
{
  enca_u32 main_tid = enca_thread_self_id ();
  CHECK (main_tid != 0);

  atomic_store (&main_tid_seen, 0);
  enca_thread t;
  enca_thread_create (&t, "test-tid", tid_worker, NULL);
  enca_thread_join (&t);

  CHECK (atomic_load (&main_tid_seen) != 0);
  CHECK (atomic_load (&main_tid_seen) != main_tid);
}

void
run_test_thread (void)
{
  enca_test_run_suite ("thread/create-join", test_thread_create_join);
  enca_test_run_suite ("thread/many-threads",
                       test_many_threads_mutex_free_counter);
  enca_test_run_suite ("thread/mutex", test_mutex_protects);
  enca_test_run_suite ("thread/cond-handshake", test_condition_handshake);
  enca_test_run_suite ("thread/timed-wait", test_timed_wait_times_out);
  enca_test_run_suite ("thread/try-lock", test_try_lock);
  enca_test_run_suite ("thread/tls", test_tls_slots);
  enca_test_run_suite ("thread/self-id", test_self_id);
}
