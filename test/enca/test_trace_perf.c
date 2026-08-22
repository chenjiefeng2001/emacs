#include "test_util.h"
#include "../../src/enca/trace/trace.h"
#include "../../src/enca/trace/profiler.h"

static void
test_counter_sum (void)
{
  enca_counter c;
  enca_counter_init (&c);

  enum { N = 800000 };

  for (int i = 0; i < N; i++)
    enca_counter_add (&c, 3);

  CHECK_EQ_U64 (enca_counter_get (&c), (enca_u64) N * 3);

  enca_counter_reset (&c);
  CHECK_EQ_U64 (enca_counter_get (&c), 0);
}

static void
test_histogram_percentiles (void)
{
  enca_histogram h;
  enca_histogram_init (&h);

  enca_u64 dummy = 0;
  CHECK_EQ_U64 (enca_histogram_percentile (&h, 50.0, &dummy),
                ENCA_ERR_NOT_FOUND);
  CHECK (ENCA_RESULT_IS_ERR (enca_histogram_percentile (&h, -1.0, &dummy)));
  CHECK (ENCA_RESULT_IS_ERR (enca_histogram_percentile (&h, 101.0, &dummy)));
  CHECK (ENCA_RESULT_IS_ERR (enca_histogram_percentile (NULL, 50.0, &dummy)));

  for (int i = 0; i < 63; i++)
    enca_histogram_record_ns (&h, ((enca_u64) 1) << i);

  enca_u64 count = 0, sum = 0, min = 0, max = 0;
  enca_histogram_stats (&h, &count, &sum, &min, &max);
  CHECK_EQ_U64 (count, 63);
  CHECK_EQ_U64 (min, 1);
  CHECK_EQ_U64 (max, ((enca_u64) 1) << 62);
  CHECK_EQ_U64 (sum,
                (((enca_u64) 1) << 63) - 1);

  enca_u64 p50 = 0, p99 = 0, p999 = 0, p100 = 0;
  CHECK_EQ_U64 (enca_histogram_percentile (&h, 50.0, &p50), ENCA_OK);
  CHECK_EQ_U64 (enca_histogram_percentile (&h, 99.0, &p99), ENCA_OK);
  CHECK_EQ_U64 (enca_histogram_percentile (&h, 99.9, &p999), ENCA_OK);
  CHECK_EQ_U64 (enca_histogram_percentile (&h, 100.0, &p100), ENCA_OK);

  CHECK (p50 > 0 && p50 <= max);
  CHECK (p99 >= p50 && p99 <= max);
  CHECK (p999 >= p99 && p999 <= max);
  CHECK (p100 >= p999 && p100 <= max);
}

static void
test_timer_measures (void)
{
  enca_timer t;
  enca_timer_begin (&t);

  volatile enca_u64 sink = 0;
  for (int i = 0; i < 100000; i++)
    sink += i;

  enca_u64 ns = enca_timer_end_ns (&t);
  CHECK (ns > 0 && ns < 10000000000ull);
  CHECK_EQ_U64 (sink != 0, 1);
}

static void
test_trace_emit_and_dump (void)
{
  enca_usize before = enca_trace_record_count ();

  enca_trace_set_enabled (true);
  CHECK (enca_trace_enabled ());

  const int n = 10;
  for (int i = 0; i < n; i++)
    enca_trace_emit (ENCA_TRACE_EVENT_PUBLISH, (enca_u64) i + 1,
                     (enca_u64) i * 2);

  enca_trace_set_enabled (false);
  CHECK (!enca_trace_enabled ());
  enca_trace_emit (ENCA_TRACE_EVENT_PUBLISH, 99999, 0);

  CHECK_EQ_U64 (enca_trace_record_count () - before, n);

  FILE *f = fopen ("trace-dump-test.json", "w");
  CHECK (f != NULL);
  if (f)
    {
      CHECK (enca_trace_dump_chrome (f));
      fclose (f);
    }

  f = fopen ("trace-dump-test.json", "r");
  if (f)
    {
      char buf[8192];
      size_t got = fread (buf, 1, sizeof buf - 1, f);
      buf[got] = 0;
      fclose (f);

      CHECK (strstr (buf, "{\"traceEvents\":[") == buf);
      CHECK (strstr (buf, "event-publish") != NULL);
      CHECK (strstr (buf, "99999") == NULL);
    }
  remove ("trace-dump-test.json");

  CHECK (!enca_trace_dump_chrome (NULL));
}

void
run_test_trace_perf (void)
{
  enca_test_run_suite ("perf/counter", test_counter_sum);
  enca_test_run_suite ("perf/histogram", test_histogram_percentiles);
  enca_test_run_suite ("perf/timer", test_timer_measures);
  enca_test_run_suite ("trace/dump", test_trace_emit_and_dump);
}
