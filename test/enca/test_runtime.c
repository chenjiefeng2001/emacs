#include "test_util.h"
#include "../../src/enca/runtime/runtime.h"

#include <stdatomic.h>

#define INPUT_PATTERN(i, slot) \
  ((unsigned char) ((i) * 31 + (slot) * 7 + 1))

static enca_u64
reference_fnv (const unsigned char *p, enca_usize n)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (enca_usize i = 0; i < n; i++)
    {
      h ^= p[i];
      h *= (enca_u64) 1099511628211ull;
    }
  return h;
}

static void
test_lifecycle (void)
{
  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 2, 64), ENCA_OK);
  CHECK_EQ_U64 (enca_runtime_current_generation (&rt), 1);
  CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 100, NULL, NULL), 0);

  CHECK_EQ_U64 (enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (2000)),
                ENCA_OK);
  enca_runtime_destroy (&rt);
}

static void
test_invalid_init (void)
{
  enca_runtime rt;
  CHECK (ENCA_RESULT_IS_ERR (enca_runtime_init (NULL, 2, 64)));
  CHECK (ENCA_RESULT_IS_ERR (enca_runtime_init (&rt, 0, 64)));
  CHECK (ENCA_RESULT_IS_ERR (
    enca_runtime_init (&rt, ENCA_RT_MAX_WORKERS + 1, 64)));
}

typedef struct collect_ctx
{
  enca_u64 *seqs;
  enca_task_result *results;
  enca_usize count;
  enca_usize cap;
} collect_ctx;

static void
collect_commit (const enca_task_result *r, void *ctx)
{
  collect_ctx *c = ctx;

  if (c->count < c->cap)
    {
      if (c->seqs)
        c->seqs[c->count] = r->task_seq;
      if (c->results)
        c->results[c->count] = *r;
    }
  c->count++;
}

static void
drain_all (enca_runtime *rt, enca_rt_commit_fn cb, void *ctx,
           enca_usize expect)
{
  enca_usize total = 0;
  enca_deadline dl = enca_deadline_from_now_ms (30000);

  while (total < expect && !enca_deadline_expired (dl))
    {
      enca_usize got = enca_runtime_poll_results (rt, 256, cb, ctx);
      total += got;
      if (got == 0)
        enca_thread_yield ();
    }
}

static void
test_commit_values_correct (void)
{
  enum { N = 2000, SLOT = 512 };
  static unsigned char input[N][SLOT];
  static enca_u64 expected[N];
  static enca_task_result got[N];

  for (int i = 0; i < N; i++)
    {
      for (int j = 0; j < SLOT; j++)
        input[i][j] = INPUT_PATTERN (i, j);
      expected[i] = reference_fnv (input[i], SLOT);
    }

  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 4, 256), ENCA_OK);

  for (int i = 0; i < N; i++)
    {
      enca_result r
        = enca_runtime_submit (&rt, (enca_object_id) (i + 1), 0, input[i],
                               SLOT);
      if (ENCA_RESULT_IS_ERR (r))
        {
          CHECK (! "submit failed");
          break;
        }
    }
  CHECK_EQ_U64 (enca_counter_get (&rt.tasks_submitted), N);

  collect_ctx cc = { NULL, got, 0, N };
  drain_all (&rt, collect_commit, &cc, N);

  CHECK_EQ_U64 (cc.count, N);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_committed), N);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_dropped_stale), 0);

  bool values_ok = true;
  bool sources_ok = true;

  for (int i = 0; i < N; i++)
    {
      enca_u64 idx = got[i].task_seq - 1;
      if (idx >= N || got[i].value != expected[idx])
        values_ok = false;
      if (got[i].source_id != (enca_object_id) (idx + 1))
        sources_ok = false;
    }
  CHECK (values_ok);
  CHECK (sources_ok);

  enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (5000));
  enca_runtime_destroy (&rt);
}

static void
test_stale_results_dropped (void)
{
  enum { OLD = 50, NEW = 50 };

  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 4, 128), ENCA_OK);

  static unsigned char big[1 << 20];
  memset (big, 0x5A, sizeof big);

  for (int i = 0; i < OLD; i++)
    CHECK_EQ_U64 (enca_runtime_submit (&rt, 100, 0, big, sizeof big),
                  ENCA_OK);

  enca_runtime_advance_generation (&rt);
  CHECK_EQ_U64 (enca_runtime_current_generation (&rt), 2);

  int marker = 0;
  for (int i = 0; i < NEW; i++)
    {
      marker = i;
      CHECK_EQ_U64 (enca_runtime_submit (&rt, 200, 0, &marker, sizeof marker),
                    ENCA_OK);
    }

  collect_ctx cc = { NULL, NULL, 0, 0 };
  {
    enca_deadline dl = enca_deadline_from_now_ms (30000);

    while (enca_counter_get (&rt.results_committed) < NEW
           && !enca_deadline_expired (dl))
      {
        enca_usize got = enca_runtime_poll_results (&rt, 128,
                                                    collect_commit, &cc);
        if (got == 0)
          enca_thread_yield ();
      }
  }

  enca_runtime_poll_results (&rt, 4096, NULL, NULL);

  CHECK_EQ_U64 (enca_counter_get (&rt.results_committed), NEW);
  CHECK (enca_counter_get (&rt.results_dropped_stale) > 0
         || enca_counter_get (&rt.tasks_cancelled_cooperative) > 0);

  enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (10000));

  CHECK_EQ_U64 (atomic_load_explicit (&rt.current_generation,
                                      memory_order_relaxed),
                2);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_committed), NEW);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_dropped_stale)
                  + enca_counter_get (&rt.results_discarded_shutdown)
                  + enca_counter_get (&rt.tasks_cancelled_cooperative),
                OLD);

  enca_runtime_destroy (&rt);
}

static void
test_single_worker_ordering (void)
{
  enum { N = 500 };

  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 1, 64), ENCA_OK);

  static enca_u64 seqs[N + 1];
  collect_ctx cc = { seqs, NULL, 0, N };

  for (int i = 0; i < N; i++)
    {
      unsigned char payload = (unsigned char) i;
      CHECK_EQ_U64 (enca_runtime_submit (&rt, 7, 0, &payload, 1), ENCA_OK);
    }

  drain_all (&rt, collect_commit, &cc, N);

  CHECK_EQ_U64 (cc.count, N);

  bool strictly_increasing = true;
  for (enca_usize i = 1; i < cc.count; i++)
    if (cc.seqs[i - 1] >= cc.seqs[i])
      {
        strictly_increasing = false;
        break;
      }
  CHECK (strictly_increasing);

  enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (5000));
  enca_runtime_destroy (&rt);
}

static void
test_shutdown_with_pending_work (void)
{
  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 3, 1024), ENCA_OK);

  static unsigned char blob[2048];
  memset (blob, 0x77, sizeof blob);

  for (int i = 0; i < 4000; i++)
    CHECK_EQ_U64 (enca_runtime_submit (&rt, 9, 0, blob, sizeof blob),
                  ENCA_OK);

  CHECK_EQ_U64 (enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (30000)),
                ENCA_OK);

  enca_u64 committed = enca_counter_get (&rt.results_committed);
  enca_u64 discarded = enca_counter_get (&rt.results_discarded_shutdown);
  enca_u64 cancelled = enca_counter_get (&rt.tasks_cancelled_cooperative);

  CHECK_EQ_U64 (committed + discarded + cancelled <= 4000, 1);

  enca_runtime_destroy (&rt);
}

static void
test_metrics_consistent (void)
{
  enum { N = 800 };

  enca_runtime rt;
  CHECK_EQ_U64 (enca_runtime_init (&rt, 2, 128), ENCA_OK);

  unsigned char p[32];
  memset (p, 3, sizeof p);

  for (int i = 0; i < N; i++)
    CHECK_EQ_U64 (enca_runtime_submit (&rt, 1, 0, p, sizeof p), ENCA_OK);

  collect_ctx cc = { NULL, NULL, 0, 0 };
  drain_all (&rt, collect_commit, &cc, N);

  CHECK_EQ_U64 (cc.count, N);

  CHECK_EQ_U64 (enca_counter_get (&rt.tasks_submitted), N);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_committed)
                  + enca_counter_get (&rt.results_dropped_stale),
                N);
  CHECK_EQ_U64 (enca_counter_get (&rt.results_dropped_stale), 0);

  enca_u64 cnt, sum, mn, mx;
  enca_histogram_stats (&rt.h_submit_ns, &cnt, &sum, &mn, &mx);
  CHECK_EQ_U64 (cnt, N);
  CHECK (mn > 0 && mx >= mn);

  enca_histogram_stats (&rt.h_poll_block_ns, &cnt, &sum, &mn, &mx);
  CHECK (cnt > 0);

  enca_u64 p99 = 0;
  CHECK_EQ_U64 (
    enca_histogram_percentile (&rt.h_complete_latency_ns, 99.0, &p99),
    ENCA_OK);
  CHECK (p99 > 0);

  enca_runtime_shutdown (&rt, enca_deadline_from_now_ms (5000));
  enca_runtime_destroy (&rt);
}

void
run_test_runtime (void)
{
  enca_test_run_suite ("runtime/lifecycle", test_lifecycle);
  enca_test_run_suite ("runtime/invalid-init", test_invalid_init);
  enca_test_run_suite ("runtime/commit-values", test_commit_values_correct);
  enca_test_run_suite ("runtime/stale-drop", test_stale_results_dropped);
  enca_test_run_suite ("runtime/ordering", test_single_worker_ordering);
  enca_test_run_suite ("runtime/shutdown-pending",
                       test_shutdown_with_pending_work);
  enca_test_run_suite ("runtime/metrics", test_metrics_consistent);
}
