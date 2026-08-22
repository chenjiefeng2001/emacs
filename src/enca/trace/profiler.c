#include "profiler.h"

#include "../base/assert.h"
#include "../time/time.h"
#include <string.h>

void
enca_counter_init (enca_counter *c)
{
  atomic_store_explicit (&c->value, 0, memory_order_relaxed);
}

void
enca_counter_add (enca_counter *c, enca_u64 n)
{
  atomic_fetch_add_explicit (&c->value, n, memory_order_relaxed);
}

enca_u64
enca_counter_get (const enca_counter *c)
{
  return atomic_load_explicit ((atomic_ullong *) &c->value,
                               memory_order_relaxed);
}

void
enca_counter_reset (enca_counter *c)
{
  atomic_store_explicit (&c->value, 0, memory_order_relaxed);
}

static unsigned
bucket_of (enca_u64 v)
{
  unsigned b = 0;
  while (v > 1)
    {
      v >>= 1;
      b++;
    }
  return b;
}

void
enca_histogram_init (enca_histogram *h)
{
  memset ((void *) h, 0, sizeof *h);
  atomic_store_explicit (&h->min_ns, UINT64_MAX, memory_order_relaxed);
}

void
enca_histogram_record_ns (enca_histogram *h, enca_u64 ns)
{
  atomic_fetch_add_explicit (&h->buckets[bucket_of (ns)], 1,
                             memory_order_relaxed);
  atomic_fetch_add_explicit (&h->sum_ns, ns, memory_order_relaxed);

  enca_u64 prev = atomic_load_explicit (&h->min_ns, memory_order_relaxed);
  while (ns < prev
         && !atomic_compare_exchange_weak_explicit (
           &h->min_ns, &prev, ns, memory_order_relaxed, memory_order_relaxed))
    ;

  prev = atomic_load_explicit (&h->max_ns, memory_order_relaxed);
  while (ns > prev
         && !atomic_compare_exchange_weak_explicit (
           &h->max_ns, &prev, ns, memory_order_relaxed, memory_order_relaxed))
    ;

  atomic_fetch_add_explicit (&h->count, 1, memory_order_relaxed);
}

enca_result
enca_histogram_percentile (const enca_histogram *h, double p, enca_u64 *out_ns)
{
  if (!h || !out_ns || p < 0.0 || p > 100.0)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_u64 count = atomic_load_explicit ((atomic_ullong *) &h->count,
                                         memory_order_relaxed);
  if (count == 0)
    return ENCA_ERR_NOT_FOUND;

  enca_u64 target = (enca_u64) ((p / 100.0) * (double) count + 0.5);
  if (target == 0)
    target = 1;
  if (target > count)
    target = count;

  enca_u64 acc = 0;
  for (unsigned b = 0; b < ENCA_HISTOGRAM_BUCKETS; b++)
    {
      acc += atomic_load_explicit ((atomic_ullong *) &h->buckets[b],
                                   memory_order_relaxed);
      if (acc >= target)
        {
          enca_u64 lo = b == 0 ? 0 : ((enca_u64) 1 << (b - 1));
          enca_u64 hi = ((enca_u64) 1 << b);
          *out_ns = lo == hi ? lo : lo + (hi - lo) / 2;
          return ENCA_OK;
        }
    }
  *out_ns = atomic_load_explicit ((atomic_ullong *) &h->max_ns,
                                  memory_order_relaxed);
  return ENCA_OK;
}

void
enca_histogram_stats (const enca_histogram *h, enca_u64 *out_count,
                      enca_u64 *out_sum_ns, enca_u64 *out_min_ns,
                      enca_u64 *out_max_ns)
{
  if (out_count)
    *out_count = atomic_load_explicit ((atomic_ullong *) &h->count,
                                       memory_order_relaxed);
  if (out_sum_ns)
    *out_sum_ns = atomic_load_explicit ((atomic_ullong *) &h->sum_ns,
                                        memory_order_relaxed);
  if (out_min_ns)
    *out_min_ns = atomic_load_explicit ((atomic_ullong *) &h->min_ns,
                                        memory_order_relaxed);
  if (out_max_ns)
    *out_max_ns = atomic_load_explicit ((atomic_ullong *) &h->max_ns,
                                        memory_order_relaxed);
}
