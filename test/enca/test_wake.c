/* EVS-3.0 correctness matrix for the runtime notification primitive
   and its scheduler wiring (bench/enca/evs3/EVS3.md section 5).

   Gates are COUNTING invariants, not timing assertions; timeouts are
   generous and only guard against hangs. */

#include "test_util.h"

#include "../../src/enca/scheduler/scheduler.h"
#include "../../src/enca/time/time.h"
#include "../../src/enca/wake/wake.h"

#include <stdlib.h>

/* ---------------- primitive suites ---------------- */

static void
wake_single (void)
{
  enca_wake_source w;
  CHECK_EQ_U64 (enca_wake_init (&w), ENCA_OK);

  /* IDLE: short wait times out, token untouched. */
  CHECK (!enca_wake_wait (&w, 20 * 1000 * 1000));
  CHECK (!enca_wake_poll (&w));

  enca_wake_notify (&w);
  CHECK (enca_wake_poll (&w));  /* NOTIFIED consumed */
  CHECK (!enca_wake_poll (&w)); /* exactly one */

  enca_wake_notify (&w);
  CHECK (enca_wake_wait (&w, 1000 * 1000 * 1000));
  CHECK (!enca_wake_wait (&w, 10 * 1000 * 1000));

  enca_wake_destroy (&w);
}

static void
wake_burst (void)
{
  enca_wake_source w;
  CHECK_EQ_U64 (enca_wake_init (&w), ENCA_OK);

  /* M notifies collapse into at most one outstanding wake. */
  for (int i = 0; i < 64; i++)
    enca_wake_notify (&w);
  CHECK (enca_wake_wait (&w, 1000 * 1000 * 1000));
  CHECK (!enca_wake_wait (&w, 5 * 1000 * 1000));
  CHECK (!enca_wake_poll (&w));

  enca_wake_destroy (&w);
}

static void
wake_drain_race (void)
{
  /* Contract-critical interleaving: producer enqueues+notifies inside
     the consumer's empty-poll -> wait() window.  wait() must return
     immediately because notify() landed before the mutex was taken. */
  enca_wake_source w;
  CHECK_EQ_U64 (enca_wake_init (&w), ENCA_OK);
  CHECK (!enca_wake_poll (&w)); /* "queue empty" as observed by main */
  enca_wake_notify (&w);        /* producer lands in the window      */
  CHECK (enca_wake_wait (&w, 200 * 1000 * 1000));
  CHECK (!enca_wake_poll (&w));
  enca_wake_destroy (&w);
}

static void
wake_notify_during_drain (void)
{
  enca_wake_source w;
  CHECK_EQ_U64 (enca_wake_init (&w), ENCA_OK);

  enca_wake_notify (&w);
  CHECK (enca_wake_wait (&w, 1000 * 1000 * 1000)); /* now DRAINING */

  /* Notification arriving while MAIN_DRAINING must make the next
     wait() return immediately. */
  enca_wake_notify (&w);
  CHECK (enca_wake_wait (&w, 200 * 1000 * 1000));
  CHECK (!enca_wake_wait (&w, 10 * 1000 * 1000));

  enca_wake_destroy (&w);
}

/* ---------------- storm: producers vs draining consumer ---------------- */

typedef struct
{
  enca_wake_source *wake;
  _Atomic long pending;         /* work items awaiting drain        */
  _Atomic long handled;         /* work items drained by consumer   */
} storm;

static enca_result
storm_producer (void *arg)
{
  storm *s = arg;
  for (int i = 0; i < 250; i++)
    {
      atomic_fetch_add (&s->pending, 1);  /* enqueue FIRST          */
      enca_wake_notify (s->wake);         /* then notify            */
    }
  return ENCA_OK;
}

static void
wake_producer_storm (void)
{
  enum { P = 4 };
  enca_wake_source w;
  storm s;
  enca_thread th[P];

  CHECK_EQ_U64 (enca_wake_init (&w), ENCA_OK);
  atomic_store (&s.pending, 0);
  atomic_store (&s.handled, 0);
  s.wake = &w;

  for (int i = 0; i < P; i++)
    CHECK_EQ_U64 (enca_thread_create (&th[i], "wake-storm",
                                      storm_producer, &s), ENCA_OK);

  const long total = P * 250;
  while (atomic_load (&s.handled) < total)
    {
      if (enca_wake_wait (&w, 50 * 1000 * 1000))
        {
          /* Drain EVERYTHING currently pending in one pass. */
          long got = atomic_exchange (&s.pending, 0);
          atomic_fetch_add (&s.handled, got);
        }
    }

  for (int i = 0; i < P; i++)
    enca_thread_join (&th[i]);

  /* No lost wakeups: every produced item was eventually drained. */
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&s.handled), total);
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&s.pending), 0);
  /* A trailing notification may legitimately remain set when its
     producer ran after the last drain pass (produce-then-notify
     order); absorbing it must find zero outstanding items. */
  if (enca_wake_poll (&w))
    CHECK_EQ_U64 ((unsigned long long) atomic_exchange (&s.pending, 0),
                  0);
  enca_wake_destroy (&w);
}

/* ---------------- scheduler integration ---------------- */

typedef struct
{
  enca_scheduler sched;
  enca_wake_source wake;
  _Atomic long good;            /* EXECUTED results                  */
  _Atomic long wasted;          /* drop results routed through poll  */
} sharness;

static int
sh_exec (const enca_sched_task *t, void *ctx, enca_u64 *out)
{
  (void) t;
  (void) ctx;
  *out = 42;
  return 0;
}

static void
sh_commit (const enca_sched_result *r, void *ctx)
{
  sharness *h = ctx;
  if (r->status == ENCA_TSTAT_EXECUTED)
    atomic_fetch_add (&h->good, 1);
  else
    atomic_fetch_add (&h->wasted, 1);
}

/* Leaf-safe observer required by the scheduler contract. */
static void
sh_notify (void *ctx)
{
  enca_wake_notify ((enca_wake_source *) ctx);
}

static enca_sched_task
wmk (enca_object_id doc, enca_u64 gen, enca_u64 rev)
{
  enca_sched_task t;
  memset (&t, 0, sizeof t);
  t.document_id = doc;
  t.cls = ENCA_TCLASS_SYSTEM;
  t.generation = gen;
  t.document_revision = rev;
  t.urgency = ENCA_URGENCY_NORMAL;
  t.deadline_ns = ENCA_DEADLINE_NONE;
  return t;
}

/* Pump via wake source until COMMIT returns TARGET hits (good+wasted)
   or the deadline passes; returns true on success. */
static bool
sh_pump_until (sharness *h, long target, unsigned timeout_ms)
{
  enca_u64 deadline
    = enca_monotonic_now_ns () + (enca_u64) timeout_ms * 1000000ull;
  for (;;)
    {
      enca_sched_poll (&h->sched, sh_commit, h);
      long done = atomic_load (&h->good) + atomic_load (&h->wasted);
      if (done >= target)
        return true;
      if (enca_monotonic_now_ns () >= deadline)
        return false;
      enca_wake_wait (&h->wake, 2 * 1000 * 1000);
    }
}

static void
wake_sched_shutdown (void)
{
  sharness h;
  CHECK_EQ_U64 (enca_sched_init (&h.sched), ENCA_OK);
  CHECK_EQ_U64 (enca_wake_init (&h.wake), ENCA_OK);
  atomic_store (&h.good, 0);
  atomic_store (&h.wasted, 0);

  enca_sched_set_result_notify (&h.sched,
                                sh_notify,
                                &h.wake);
  CHECK_EQ_U64 (enca_sched_start_workers (&h.sched, 2, sh_exec, NULL),
                ENCA_OK);

  enum { N = 64 };
  for (int i = 0; i < N; i++)
    {
      enca_sched_task t = wmk (1, 1, (enca_u64) i + 1);
      CHECK_EQ_U64 ((int) enca_sched_submit (&h.sched, &t, NULL),
                    (int) ENCA_ADMIT_ACCEPTED);
    }

  /* Wakeup-driven pumping reaches every result with no fixed-sleep
     polling. */
  CHECK (sh_pump_until (&h, N, 10000));
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&h.good), N);

  const enca_scheduler_stats *st = enca_sched_stats (&h.sched);
  CHECK_EQ_U64 ((unsigned long long)
                   atomic_load (&st->results_total), N);

  enca_sched_shutdown (&h.sched);

  /* Post-shutdown: submits rejected outright, nothing queued, no
     results and no notifications (EVS-3 erratum: this gate was
     documented but not enforced before). */
  enca_sched_task t = wmk (1, 1, 999);
  CHECK_EQ_U64 ((int) enca_sched_submit (&h.sched, &t, NULL),
                (int) ENCA_ADMIT_REJECTED);
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&st->shutdown_rejects),
                1);
  CHECK_EQ_U64 ((int) enca_sched_poll (&h.sched, sh_commit, &h), 0);
  CHECK_EQ_U64 ((unsigned long long)
                   atomic_load (&st->results_total), N);

  /* A trailing notification from the last result's hook may still be
     pending (produce-then-notify order); absorbing it twice must
     reach steady state with zero outstanding work. */
  (void) enca_wake_poll (&h.wake);
  CHECK (!enca_wake_poll (&h.wake));

  /* Destroy order per contract: workers joined -> drain -> sink. */
  enca_sched_destroy (&h.sched);
  enca_wake_destroy (&h.wake);
}

static void
wake_generation_reset (void)
{
  sharness h;
  CHECK_EQ_U64 (enca_sched_init (&h.sched), ENCA_OK);
  CHECK_EQ_U64 (enca_wake_init (&h.wake), ENCA_OK);
  atomic_store (&h.good, 0);
  atomic_store (&h.wasted, 0);

  enca_sched_set_result_notify (&h.sched,
                                sh_notify,
                                &h.wake);
  CHECK_EQ_U64 (enca_sched_start_workers (&h.sched, 1, sh_exec, NULL),
                ENCA_OK);

  /* Batch A at generation 1, then bump: all stale at dispatch gate.
     Drops are RESULTS too -- they must still notify so the consumer
     can observe and filter them. */
  enum { NA = 8 };
  for (int i = 0; i < NA; i++)
    {
      enca_sched_task t = wmk (1, 1, (enca_u64) i + 1);
      CHECK_EQ_U64 ((int) enca_sched_submit (&h.sched, &t, NULL),
                    (int) ENCA_ADMIT_ACCEPTED);
    }
  enca_sched_advance_generation (&h.sched);
  CHECK (sh_pump_until (&h, NA, 10000));
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&h.good), 0);
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&h.wasted), NA);

  const enca_scheduler_stats *st = enca_sched_stats (&h.sched);
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&st->executed), 0);
  CHECK_EQ_U64 ((unsigned long long)
                   atomic_load (&st->dropped_stale_dispatch), NA);

  /* Batch B at the NEW generation executes normally through the same
     wakeup path. */
  enca_u64 gen = enca_sched_current_generation (&h.sched);
  enum { NB = 4 };
  for (int i = 0; i < NB; i++)
    {
      enca_sched_task t = wmk (1, gen, (enca_u64) i + 100);
      CHECK_EQ_U64 ((int) enca_sched_submit (&h.sched, &t, NULL),
                    (int) ENCA_ADMIT_ACCEPTED);
    }
  CHECK (sh_pump_until (&h, NA + NB, 10000));
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&h.good), NB);
  CHECK_EQ_U64 ((unsigned long long) atomic_load (&h.wasted), NA);

  enca_sched_shutdown (&h.sched);
  enca_sched_destroy (&h.sched);
  enca_wake_destroy (&h.wake);
}

void
run_test_wake (void)
{
  enca_test_run_suite ("wake/single", wake_single);
  enca_test_run_suite ("wake/burst", wake_burst);
  enca_test_run_suite ("wake/drain-race", wake_drain_race);
  enca_test_run_suite ("wake/notify-during-drain",
                       wake_notify_during_drain);
  enca_test_run_suite ("wake/producer-storm", wake_producer_storm);
  enca_test_run_suite ("wake/shutdown", wake_sched_shutdown);
  enca_test_run_suite ("wake/generation-reset", wake_generation_reset);
}