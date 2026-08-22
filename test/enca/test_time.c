#include "test_util.h"
#include "../../src/enca/time/time.h"
#include "../../src/enca/thread/thread.h"

static void
test_monotonic_non_decreasing (void)
{
  enca_timestamp_ns prev = enca_monotonic_now_ns ();

  for (int i = 0; i < 1000; i++)
    {
      enca_timestamp_ns now = enca_monotonic_now_ns ();
      CHECK (now >= prev);
      if (now < prev)
        return;
      prev = now;
    }
}

static void
test_monotonic_advances (void)
{
  enca_timestamp_ns start = enca_monotonic_now_ns ();
  volatile enca_u64 sink = 0;

  while (enca_monotonic_now_ns () - start < ENCA_NS_PER_MS)
    sink++;

  CHECK_EQ_U64 (sink != 0, 1);
}

static void
test_wallclock_plausible (void)
{
  enca_timestamp_ns wall = enca_wallclock_now_ns ();

  const enca_timestamp_ns jan_2020_ns
    = (enca_timestamp_ns) 1577836800 * ENCA_NS_PER_S;
  CHECK (wall > jan_2020_ns);
}

static void
test_deadline_math (void)
{
  enca_deadline none = ENCA_DEADLINE_NONE;
  CHECK (enca_deadline_is_none (none));
  CHECK (!enca_deadline_expired (none));
  CHECK_EQ_U64 (enca_deadline_remaining_ns (none), UINT64_MAX);

  enca_deadline past = { enca_monotonic_now_ns () - 1000000 };
  CHECK (enca_deadline_expired (past));
  CHECK_EQ_U64 (enca_deadline_remaining_ns (past), 0);

  enca_deadline soon = enca_deadline_from_now_ms (5);
  CHECK (!enca_deadline_is_none (soon));
  CHECK (!enca_deadline_expired (soon));
  enca_u64 rem = enca_deadline_remaining_ns (soon);
  CHECK (rem > 0 && rem <= 5 * ENCA_NS_PER_MS);

  while (!enca_deadline_expired (soon))
    enca_thread_yield ();
  CHECK_EQ_U64 (enca_deadline_remaining_ns (soon), 0);
}

static void
test_thread_cpu_time (void)
{
  volatile enca_u64 sink = 0;
  for (int i = 0; i < 100000; i++)
    sink += i;

  CHECK_EQ_U64 (sink != 0, 1);
}

void
run_test_time (void)
{
  enca_test_run_suite ("time/monotonic-order", test_monotonic_non_decreasing);
  enca_test_run_suite ("time/monotonic-advance", test_monotonic_advances);
  enca_test_run_suite ("time/wallclock", test_wallclock_plausible);
  enca_test_run_suite ("time/deadline", test_deadline_math);
  enca_test_run_suite ("time/cpu", test_thread_cpu_time);
}
