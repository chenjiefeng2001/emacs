#include "test_util.h"
#include "../../src/enca/cancel/cancel.h"
#include "../../src/enca/thread/thread.h"

#include <stdatomic.h>

#define RACE_REPLACES 100000
#define RACE_REPLACERS 2
#define RACE_READERS 4
#define RACE_MIN_READS (RACE_READERS * 1000ull)

typedef struct race_ctx
{
  _Atomic (enca_cancel_source *) slot;
  enca_mutex slot_lock;
  _Atomic bool stop;
  _Atomic unsigned long long reads;
} race_ctx;

typedef enca_result (*race_thread_fn) (void *);

static enca_result
replacer_borrow (void *arg)
{
  race_ctx *c = arg;

  for (int i = 0; i < RACE_REPLACES; i++)
    {
      enca_cancel_source *old = atomic_exchange_explicit (&c->slot, NULL,
                                                          memory_order_acq_rel);
      enca_cancel_source_cancel (old);
      enca_thread_yield ();
      enca_cancel_source_release (old);

      enca_cancel_source *fresh = NULL;
      if (enca_cancel_source_create (&fresh) == ENCA_OK)
        atomic_store_explicit (&c->slot, fresh, memory_order_release);
    }
  return ENCA_OK;
}

static enca_result
reader_borrow (void *arg)
{
  race_ctx *c = arg;

  while (!atomic_load_explicit (&c->stop, memory_order_acquire))
    {
      enca_cancel_source *src = atomic_load_explicit (&c->slot,
                                                      memory_order_acquire);
      if (src)
        {
          enca_thread_yield ();
          (void) enca_cancel_source_is_cancelled (src);
          atomic_fetch_add_explicit (&c->reads, 1, memory_order_relaxed);
        }
      else
        enca_thread_yield ();
    }
  return ENCA_OK;
}

static enca_result
replacer_retain (void *arg)
{
  race_ctx *c = arg;

  for (int i = 0; i < RACE_REPLACES; i++)
    {
      enca_mutex_lock (&c->slot_lock);
      enca_cancel_source *old = atomic_exchange_explicit (&c->slot, NULL,
                                                          memory_order_acq_rel);
      enca_mutex_unlock (&c->slot_lock);

      enca_cancel_source_cancel (old);
      enca_cancel_source_release (old);

      enca_cancel_source *fresh = NULL;
      if (enca_cancel_source_create (&fresh) == ENCA_OK)
        {
          enca_mutex_lock (&c->slot_lock);
          atomic_store_explicit (&c->slot, fresh, memory_order_release);
          enca_mutex_unlock (&c->slot_lock);
        }
    }
  return ENCA_OK;
}

static enca_result
reader_retain (void *arg)
{
  race_ctx *c = arg;

  while (!atomic_load_explicit (&c->stop, memory_order_acquire))
    {
      enca_mutex_lock (&c->slot_lock);
      enca_cancel_source *src = atomic_load_explicit (&c->slot,
                                                      memory_order_acquire);
      enca_cancel_source_retain (src);
      enca_mutex_unlock (&c->slot_lock);

      if (src)
        {
          (void) enca_cancel_source_is_cancelled (src);
          atomic_fetch_add_explicit (&c->reads, 1, memory_order_relaxed);
          enca_cancel_source_release (src);
        }
      else
        enca_thread_yield ();
    }
  return ENCA_OK;
}

static void
run_race (race_ctx *c, race_thread_fn replacer, race_thread_fn reader,
          bool expect_refs_one)
{
  enca_thread replacers[RACE_REPLACERS];
  enca_thread readers[RACE_READERS];
  unsigned started_replacers = 0, started_readers = 0;

  atomic_init (&c->slot, NULL);
  atomic_init (&c->stop, false);
  atomic_init (&c->reads, 0);
  CHECK_EQ_U64 (enca_mutex_init (&c->slot_lock), ENCA_OK);

  enca_cancel_source *first = NULL;
  CHECK_EQ_U64 (enca_cancel_source_create (&first), ENCA_OK);
  atomic_store_explicit (&c->slot, first, memory_order_release);

  for (unsigned i = 0; i < RACE_READERS; i++)
    {
      char name[32];
      snprintf (name, sizeof name, "race-reader-%u", i);
      if (enca_thread_create (&readers[i], name, reader, c) == ENCA_OK)
        started_readers++;
    }

  for (unsigned i = 0; i < RACE_REPLACERS; i++)
    {
      char name[32];
      snprintf (name, sizeof name, "race-replacer-%u", i);
      if (enca_thread_create (&replacers[i], name, replacer, c) == ENCA_OK)
        started_replacers++;
    }

  for (unsigned i = 0; i < started_replacers; i++)
    CHECK_EQ_U64 (enca_thread_join (&replacers[i]), ENCA_OK);

  atomic_store_explicit (&c->stop, true, memory_order_release);

  for (unsigned i = 0; i < started_readers; i++)
    CHECK_EQ_U64 (enca_thread_join (&readers[i]), ENCA_OK);

  enca_mutex_destroy (&c->slot_lock);

  CHECK (atomic_load_explicit (&c->reads, memory_order_relaxed)
         >= RACE_MIN_READS);

  enca_cancel_source *last = atomic_exchange_explicit (&c->slot, NULL,
                                                       memory_order_acq_rel);
  CHECK (last != NULL);
  if (last)
    {
      if (expect_refs_one)
        CHECK_EQ_U64 (atomic_load_explicit (&last->refs,
                                            memory_order_relaxed),
                      1);
      enca_cancel_source_release (last);
    }
}

static void
test_borrow_unretained (void)
{
#if defined(ENCA_TEST_SANITIZER_RISKY) && !defined(ENCA_TEST_FORCE_RACE_CANARY)
  (void) 0;
#else
  race_ctx c;
  run_race (&c, replacer_borrow, reader_borrow, false);
#endif
}

static void
test_retain_on_load (void)
{
  race_ctx c;
  run_race (&c, replacer_retain, reader_retain, true);
}

void
run_test_cancel_race (void)
{
  enca_test_run_suite ("cancel-race/borrow-unretained",
                       test_borrow_unretained);
  enca_test_run_suite ("cancel-race/retain-on-load", test_retain_on_load);
}
