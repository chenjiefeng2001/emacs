#ifndef ENCA_PROFILER_H
#define ENCA_PROFILER_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../time/time.h"

#include <stdatomic.h>

typedef struct enca_counter
{
  _Atomic enca_u64 value;
} enca_counter;

void enca_counter_init (enca_counter *c);
void enca_counter_add (enca_counter *c, enca_u64 n);
enca_u64 enca_counter_get (const enca_counter *c);
void enca_counter_reset (enca_counter *c);

#define ENCA_HISTOGRAM_BUCKETS 65

typedef struct enca_histogram
{
  _Atomic enca_u64 buckets[ENCA_HISTOGRAM_BUCKETS];
  _Atomic enca_u64 sum_ns;
  _Atomic enca_u64 count;
  _Atomic enca_u64 min_ns;
  _Atomic enca_u64 max_ns;
} enca_histogram;

void enca_histogram_init (enca_histogram *h);
void enca_histogram_record_ns (enca_histogram *h, enca_u64 ns);

enca_result enca_histogram_percentile (const enca_histogram *h, double p,
                                       enca_u64 *out_ns);
void enca_histogram_stats (const enca_histogram *h, enca_u64 *out_count,
                           enca_u64 *out_sum_ns, enca_u64 *out_min_ns,
                           enca_u64 *out_max_ns);

typedef struct enca_timer
{
  enca_timestamp_ns start_ns;
} enca_timer;

ENCA_INLINE void
enca_timer_begin (enca_timer *t)
{
  t->start_ns = enca_monotonic_now_ns ();
}

ENCA_INLINE enca_u64
enca_timer_end_ns (enca_timer *t)
{
  return enca_monotonic_now_ns () - t->start_ns;
}

#endif
