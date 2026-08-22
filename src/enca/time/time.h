#ifndef ENCA_TIME_H
#define ENCA_TIME_H

#include "../base/types.h"
#include "../base/attributes.h"

#define ENCA_NS_PER_US ((enca_u64) 1000)
#define ENCA_NS_PER_MS ((enca_u64) 1000000)
#define ENCA_NS_PER_S ((enca_u64) 1000000000)

enca_timestamp_ns enca_monotonic_now_ns (void);
enca_timestamp_ns enca_wallclock_now_ns (void);
enca_timestamp_ns enca_thread_cpu_time_ns (void);

typedef struct enca_deadline
{
  enca_timestamp_ns abs_ns;
} enca_deadline;

#define ENCA_DEADLINE_NONE ((enca_deadline) { UINT64_MAX })

ENCA_INLINE enca_deadline
enca_deadline_from_now_ns (enca_u64 ns)
{
  return (enca_deadline) { enca_monotonic_now_ns () + ns };
}

ENCA_INLINE enca_deadline
enca_deadline_from_now_ms (enca_u64 ms)
{
  return enca_deadline_from_now_ns (ms * ENCA_NS_PER_MS);
}

ENCA_INLINE bool
enca_deadline_is_none (enca_deadline d)
{
  return d.abs_ns == UINT64_MAX;
}

ENCA_INLINE bool
enca_deadline_expired (enca_deadline d)
{
  if (enca_deadline_is_none (d))
    return false;
  return enca_monotonic_now_ns () >= d.abs_ns;
}

ENCA_INLINE enca_u64
enca_deadline_remaining_ns (enca_deadline d)
{
  if (enca_deadline_is_none (d))
    return UINT64_MAX;
  enca_timestamp_ns now = enca_monotonic_now_ns ();
  return now >= d.abs_ns ? 0 : d.abs_ns - now;
}

#endif
