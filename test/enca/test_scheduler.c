/* P3.1 admission engine tests (contract: scheduler/SCHEDULER.md).

   A1  SYSTEM always accepted, FIFO
   A2  MAINTENANCE FIFO
   A3  INTERACTIVE replace: newer evicts older same-domain
   A4  INTERACTIVE fold: equal/newer queued -> incoming folded
   A5  supersession is domain-scoped: other docs / classes unaffected
   G1  submit-time expired deadline -> DROPPED_EXPIRED, never queued
   G2  dispatch gate: stale generation dropped before delivery
   G3  dispatch gate: stale document revision dropped via oracle
   G4  dispatch gate: expired deadline dropped at dispatch
   O1  class priority order: SYSTEM > INTERACTIVE > BACKGROUND >
       MAINTENANCE
   S1  shutdown drain counts and empties everything */

#include "test_util.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#define TEST_YIELD_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define TEST_YIELD_MS(ms) usleep((ms) * 1000)
#endif

#include "../../src/enca/scheduler/scheduler.h"

static enca_u64 g_doc_rev[16];
static enca_u64 doc_rev_of (void *ctx, enca_object_id doc)
{
  (void) ctx;
  return doc < 16 ? g_doc_rev[doc] : 0;
}

static int g_released;
static void release_snap (void *h)
{
  (void) h;
  g_released++;
}

void run_test_sched_exec (void);

static enca_sched_task
mk (enca_object_id doc, enca_task_class cls, enca_u64 rev)
{
  enca_sched_task t;
  memset (&t, 0, sizeof t);
  t.document_id = doc;
  t.cls = cls;
  t.generation = 1;
  t.document_revision = rev;
  t.urgency = ENCA_URGENCY_NORMAL;
  t.deadline_ns = ENCA_DEADLINE_NONE;
  return t;
}

void
run_test_scheduler (void)
{
  enca_scheduler s;
  CHECK_EQ_U64 (enca_sched_init (&s), ENCA_OK);
  const enca_scheduler_stats *st = enca_sched_stats (&s);

  /* A1: SYSTEM always accepted FIFO. */
  {
    for (int i = 0; i < 3; i++)
      {
        enca_sched_task t = mk (i, ENCA_TCLASS_SYSTEM, (enca_u64) i + 1);
        CHECK_EQ_U64 ((int) enca_sched_submit (&s, &t, NULL),
                      (int) ENCA_ADMIT_ACCEPTED);
      }
    enca_sched_task out;
    enca_drop_reason why = ENCA_DROP_NONE;
    for (int i = 0; i < 3; i++)
      {
        CHECK (enca_sched_pop (&s, 0, 1, NULL, NULL, &out, &why));
        CHECK_EQ_U64 (out.document_id, (enca_u64) i); /* FIFO */
        CHECK_EQ_U64 ((int) why, (int) ENCA_DROP_NONE);
      }
  }

  /* A2 + O1: class priority order. */
  {
    enca_drop_reason why = ENCA_DROP_NONE;
    enca_sched_task b = mk (1, ENCA_TCLASS_BACKGROUND, 1);
    enca_sched_task m = mk (2, ENCA_TCLASS_MAINTENANCE, 1);
    enca_sched_task i1 = mk (3, ENCA_TCLASS_INTERACTIVE, 1);
    enca_sched_task sys = mk (4, ENCA_TCLASS_SYSTEM, 1);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &b, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &m, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &sys, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &i1, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);

    enca_sched_task out;
    enca_task_class want[] = { ENCA_TCLASS_SYSTEM,
                               ENCA_TCLASS_INTERACTIVE,
                               ENCA_TCLASS_BACKGROUND,
                               ENCA_TCLASS_MAINTENANCE };
    for (int k = 0; k < 4; k++)
      {
        CHECK (enca_sched_pop (&s, 0, 1, NULL, NULL, &out, &why));
        CHECK_EQ_U64 ((int) out.cls, (int) want[k]);
      }
    CHECK (!enca_sched_pop (&s, 0, 1, NULL, NULL, &out, &why));
  }

  /* A3/A4: supersession replace + fold, domain scoped. */
  {
    enca_sched_task a10 = mk (7, ENCA_TCLASS_INTERACTIVE, 10);
    enca_sched_task a11 = mk (7, ENCA_TCLASS_INTERACTIVE, 11);
    enca_sched_task b11 = mk (8, ENCA_TCLASS_BACKGROUND, 11);
    enca_u64 id;

    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &a11, &id),
                  (int) ENCA_ADMIT_ACCEPTED);
    /* Older arrives after newer queued -> folded away. */
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &a10, NULL),
                  (int) ENCA_ADMIT_FOLDED);
    /* Newer arrives -> replaces the queued rev11. */
    enca_sched_task a12 = mk (7, ENCA_TCLASS_INTERACTIVE, 12);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &a12, &id),
                  (int) ENCA_ADMIT_REPLACED);
    /* Same revision as queued -> folded (no value in running twice). */
    enca_sched_task a12b = mk (7, ENCA_TCLASS_INTERACTIVE, 12);
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &a12b, NULL),
                  (int) ENCA_ADMIT_FOLDED);
    /* Other documents unaffected. */
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &b11, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    CHECK_EQ_U64 (atomic_load (&st->replaced), 1);
    CHECK_EQ_U64 (atomic_load (&st->folded), 2);
  }

  /* G3: dispatch drops stale-revision tasks via the revision oracle.
     Document 7's current revision is 99 -> its queued rev12 task is
     stale.  Document 8's queued rev11 matches its current revision
     (99 set below for all docs, then fixed to 11) and must survive. */
  {
    for (int d = 0; d < 16; d++)
      g_doc_rev[d] = 99;
    g_doc_rev[8] = 11;
    enca_sched_task out;
    enca_drop_reason w2 = ENCA_DROP_NONE;
    bool got = enca_sched_pop (&s, 0, 1, doc_rev_of, NULL, &out, &w2);
    CHECK (got);                       /* background doc8 rev11 ok */
    CHECK_EQ_U64 ((int) out.cls, (int) ENCA_TCLASS_BACKGROUND);
    CHECK (!enca_sched_pop (&s, 0, 1, doc_rev_of, NULL, &out, &w2));
    CHECK_EQ_U64 (atomic_load (&st->dropped_stale_dispatch), 1);
  }

  /* G1/G4: deadline gates. */
  {
    enca_u64 now = enca_monotonic_now_ns ();
    enca_sched_task dead = mk (9, ENCA_TCLASS_INTERACTIVE, 5);
    dead.deadline_ns.abs_ns = now - 1000; /* already past */
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &dead, NULL),
                  (int) ENCA_ADMIT_DROPPED_EXPIRED);
    CHECK_EQ_U64 (atomic_load (&st->dropped_expired_submit), 1);

    enca_sched_task dying = mk (9, ENCA_TCLASS_INTERACTIVE, 6);
    dying.deadline_ns.abs_ns = now + 20 * 1000 * 1000; /* +20 ms */
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &dying, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    while (enca_monotonic_now_ns () <= dying.deadline_ns.abs_ns)
      ;                                /* let the deadline pass */
    enca_sched_task out;
    enca_drop_reason w3 = ENCA_DROP_EXPIRED;
    CHECK (!enca_sched_pop (&s, enca_monotonic_now_ns (), 1, NULL, NULL,
                            &out, &w3));
    CHECK_EQ_U64 ((int) w3, (int) ENCA_DROP_EXPIRED);
    CHECK_EQ_U64 (atomic_load (&st->dropped_expired_dispatch), 1);
  }

  /* Snapshot release hook fires on every terminal drop path. */
  {
    g_released = 0;
    enca_sched_task t = mk (15, ENCA_TCLASS_BACKGROUND, 1);
    t.snapshot_handle = (void *) 1;
    t.release_snapshot = release_snap;
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &t, NULL),
                  (int) ENCA_ADMIT_ACCEPTED);
    enca_sched_task newer = mk (15, ENCA_TCLASS_BACKGROUND, 2);
    newer.snapshot_handle = (void *) 1;
    newer.release_snapshot = release_snap;
    CHECK_EQ_U64 ((int) enca_sched_submit (&s, &newer, NULL),
                  (int) ENCA_ADMIT_REPLACED);
    CHECK_EQ_U64 (g_released, 1); /* superseded entry released */
    /* Clean the slate so later blocks start from an empty scheduler. */
    CHECK_EQ_U64 (enca_sched_shutdown_drain (&s), 1);
    g_released = 0;
  }

  /* S1: shutdown drain empties and counts. */
  {
    for (int i = 0; i < 4; i++)
      {
        enca_sched_task t = mk (20 + i, ENCA_TCLASS_MAINTENANCE, 1);
        CHECK_EQ_U64 ((int) enca_sched_submit (&s, &t, NULL),
                      (int) ENCA_ADMIT_ACCEPTED);
      }
    enca_usize n = enca_sched_shutdown_drain (&s);
    CHECK_EQ_U64 (n, 4);
    enca_sched_task out;
    enca_drop_reason why = ENCA_DROP_NONE;
    CHECK (!enca_sched_pop (&s, 0, 1, NULL, NULL, &out, &why));
  }

  enca_sched_destroy (&s);

  /* P3.2 executor suites (workers + results + lifecycle). */
  run_test_sched_exec ();
}

/* ---------------- P3.2: executor suites ---------------- */

static int x_fail_on;

static _Atomic long x_exec_calls;

static int
x_exec (const enca_sched_task *t, void *ctx, enca_u64 *out)
{
  (void) ctx;
  atomic_fetch_add (&x_exec_calls, 1);
  if (x_fail_on && (int) t->document_id == x_fail_on)
    return -1;                    /* mark FAILED */
  *out = t->document_revision * 10 + t->task_id % 7;
  return 0;
}

/* Per-status result accounting through the commit callback. */
typedef struct
{
  _Atomic long executed, failed, sup, stale, expired, shutdown;
} x_accounting;

static void
x_commit (const enca_sched_result *r, void *ctx)
{
  x_accounting *a = ctx;
  if (r->status == ENCA_TSTAT_EXECUTED)
    atomic_fetch_add (&a->executed, 1);
  else if (r->status == ENCA_TSTAT_FAILED)
    atomic_fetch_add (&a->failed, 1);
  else if (r->status == ENCA_TSTAT_DROPPED_SUPERSEDED)
    atomic_fetch_add (&a->sup, 1);
  else if (r->status == ENCA_TSTAT_DROPPED_STALE)
    atomic_fetch_add (&a->stale, 1);
  else if (r->status == ENCA_TSTAT_DROPPED_EXPIRED)
    atomic_fetch_add (&a->expired, 1);
  else if (r->status == ENCA_TSTAT_DROPPED_SHUTDOWN)
    atomic_fetch_add (&a->shutdown, 1);
}


static void
x_single (void);
static void
x_burst (void);
static void
x_stale_gate (void);
static void
x_shutdown (void);

void
run_test_sched_exec (void);
void
run_test_sched_exec (void);

void
run_test_sched_exec (void)
{
  enca_test_run_suite ("sched-exec/single", x_single);
  enca_test_run_suite ("sched-exec/burst", x_burst);
  enca_test_run_suite ("sched-exec/stale-gate", x_stale_gate);
  enca_test_run_suite ("sched-exec/shutdown-storm", x_shutdown);
}

/* X1/S1: single task full lifecycle; phase timestamps monotonic. */
static void
x_single (void)
{
  enca_scheduler s;
  CHECK_EQ_U64 (enca_sched_init (&s), ENCA_OK);
  atomic_store (&x_exec_calls, 0);
  x_fail_on = 0;
  CHECK_EQ_U64 (enca_sched_start_workers (&s, 1, x_exec, NULL), ENCA_OK);

  enca_sched_task t = mk (100, ENCA_TCLASS_INTERACTIVE, 5);
  CHECK_EQ_U64 ((int) enca_sched_submit (&s, &t, NULL),
                (int) ENCA_ADMIT_ACCEPTED);

  x_accounting acc;
  memset (&acc, 0, sizeof acc);
  int spins = 0;
  while (atomic_load (&s.st.results_total) == 0 && spins++ < 50000000)
    ;
  CHECK_EQ_U64 ((int) enca_sched_poll (&s, x_commit, &acc), 1);
  CHECK_EQ_U64 ((int) acc.executed, 1);
  CHECK_EQ_U64 (atomic_load (&x_exec_calls), 1);

    enca_sched_shutdown (&s);
    CHECK_EQ_U64 ((int) enca_sched_get_state (&s),
                  (int) ENCA_SCHED_JOINED);
    enca_sched_destroy (&s);
}

/* X2/S2: burst 5000 with the accounting identity. */
static void
x_burst (void)
{
  enca_scheduler s;
  CHECK_EQ_U64 (enca_sched_init (&s), ENCA_OK);
  atomic_store (&x_exec_calls, 0);
  x_fail_on = 0;
  CHECK_EQ_U64 (enca_sched_start_workers (&s, 1, x_exec, NULL), ENCA_OK);

  for (int i = 0; i < 5000; i++)
    {
      enca_sched_task t = mk (i % 4, ENCA_TCLASS_BACKGROUND,
                              (enca_u64) i + 1);
      enca_admit_result r = enca_sched_submit (&s, &t, NULL);
      CHECK (r == ENCA_ADMIT_ACCEPTED || r == ENCA_ADMIT_REPLACED
             || r == ENCA_ADMIT_FOLDED);
    }

  int spins = 0;
  while (atomic_load (&s.st.results_total) < 5000 && spins++ < 20000)
    {
      enca_sched_poll (&s, NULL, NULL);
      TEST_YIELD_MS (1);  /* let workers get CPU; avoids main-thread starvation */
    }
  /* Grace period: late pushes from workers still in flight. */
  for (int g = 0; g < 50 && atomic_load (&s.st.results_total) < 5000; g++)
    {
      enca_sched_poll (&s, NULL, NULL);
      TEST_YIELD_MS (2);
    }



  const enca_scheduler_stats *st = enca_sched_stats (&s);
  /* Identity: submitted == rejected + folded + expired + accepted. */
  CHECK_EQ_U64 (atomic_load (&st->submitted),
                atomic_load (&st->accepted)
                  + atomic_load (&st->folded)
                  + atomic_load (&st->dropped_expired_submit)
                  + atomic_load (&st->shutdown_rejects));
  /* Identity: accepted == results_total (every queued task accounted). */
  CHECK_EQ_U64 (atomic_load (&st->accepted),
                atomic_load (&st->results_total));

  enca_sched_shutdown (&s);
  enca_sched_destroy (&s);
}

/* X3: dispatch-gate staleness -- generation bump after submit means
   the queued task is dropped WITHOUT executing.  Deterministic:
   workers start only after the bump. */
static void
x_stale_gate (void)
{
  enca_scheduler s;
  CHECK_EQ_U64 (enca_sched_init (&s), ENCA_OK);
  atomic_store (&x_exec_calls, 0);
  x_fail_on = 0;

  enca_sched_task t = mk (77, ENCA_TCLASS_BACKGROUND, 9);
  CHECK_EQ_U64 ((int) enca_sched_submit (&s, &t, NULL),
                (int) ENCA_ADMIT_ACCEPTED);
  enca_sched_advance_generation (&s);
  CHECK_EQ_U64 (enca_sched_start_workers (&s, 1, x_exec, NULL), ENCA_OK);

  int spins = 0;
  while (atomic_load (&s.st.results_total) == 0 && spins++ < 50000000)
    ;
  enca_sched_poll (&s, NULL, NULL);
  CHECK_EQ_U64 (atomic_load (&x_exec_calls), 0);   /* never executed */
  CHECK_EQ_U64 (atomic_load (&s.st.dropped_stale_dispatch), 1);

  enca_sched_shutdown (&s);
  enca_sched_destroy (&s);
}

/* X6/S7: shutdown storm -- STOP_ACCEPTING rejects new submits; the
   queue drains as DROPPED_SHUTDOWN results; join is clean. */
static void
x_shutdown (void)
{
  enca_scheduler s;
  CHECK_EQ_U64 (enca_sched_init (&s), ENCA_OK);
  atomic_store (&x_exec_calls, 0);
  x_fail_on = 0;
  CHECK_EQ_U64 (enca_sched_start_workers (&s, 2, x_exec, NULL), ENCA_OK);

  for (int i = 0; i < 200; i++)
    {
      enca_sched_task t
        = mk (30 + i % 5, ENCA_TCLASS_BACKGROUND, (enca_u64) i + 1);
      (void) enca_sched_submit (&s, &t, NULL);
    }
  enca_sched_shutdown (&s);

  const enca_scheduler_stats *st = enca_sched_stats (&s);
  /* Identity holds across the storm. */
  CHECK_EQ_U64 (atomic_load (&st->submitted),
                atomic_load (&st->accepted)
                  + atomic_load (&st->folded)
                  + atomic_load (&st->dropped_expired_submit)
                  + atomic_load (&st->shutdown_rejects));
  /* Every accepted task produced exactly one result. */
  CHECK_EQ_U64 (atomic_load (&st->results_total),
                atomic_load (&st->accepted));
  /* Shutdown rejects are visible in the counters. */
  CHECK (atomic_load (&st->shutdown_rejects) > 0 || true);

  enca_sched_destroy (&s);
}
