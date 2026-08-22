#ifndef ENCA_BENCH_METRICS_H
#define ENCA_BENCH_METRICS_H

/* P2.1 measurement infrastructure: latency percentiles and reader
   threads that scan retained revisions (models workers holding
   slightly stale snapshots). */

#include "store.h"
#include "../../src/enca/thread/thread.h"

#include <stdlib.h>

static int
enca_bench_cmp_u64 (const void *a, const void *b)
{
  enca_u64 va = *(const enca_u64 *) a;
  enca_u64 vb = *(const enca_u64 *) b;
  return va < vb ? -1 : va > vb;
}

/* Returns percentile P (0..100) of v[0..n) in milliseconds.
   Sorts v in place. */
static double
enca_bench_pct_ms (enca_u64 *v, enca_usize n, double p)
{
  if (!n)
    return 0;
  qsort (v, n, sizeof *v, enca_bench_cmp_u64);
  enca_usize i = (enca_usize) (p / 100.0 * (double) n);
  if (i >= n)
    i = n - 1;
  return (double) v[i] / 1e6;
}

static _Atomic int enca_bench_readers_stop;

typedef struct
{
  const enca_store_ops *ops;
  enca_bench_rev **ring;
  enca_usize ring_len;
  enca_u64 bytes_scanned;
} enca_bench_reader_arg;

static enca_result
enca_bench_reader_main (void *arg)
{
  enca_bench_reader_arg *ra = arg;
  enca_u64 local = 0;
  while (!atomic_load (&enca_bench_readers_stop))
    for (enca_usize i = 0; i < ra->ring_len;
         i = atomic_load (&enca_bench_readers_stop) ? ra->ring_len : i + 1)
      {
        if (!ra->ring[i])
          continue;
        local += ra->ops->rev_len (ra->ring[i]);
        ra->ops->fnv_sequential (ra->ring[i]);
      }
  ra->bytes_scanned = local;
  return ENCA_OK;
}

/* Spawns READERS scanning the retained RING for READ_MS wall time;
   returns aggregate throughput in MB/s. */
static double
enca_bench_reader_phase (const enca_store_ops *ops, enca_bench_rev **ring,
                         enca_usize ring_len, unsigned readers,
                         unsigned read_ms)
{
  if (!readers || !ring_len)
    return 0;

  enca_bench_reader_arg *args = calloc (readers, sizeof *args);
  enca_thread *thr = calloc (readers, sizeof *thr);
  if (!args || !thr)
    {
      free (args);
      free (thr);
      return 0;
    }

  atomic_store (&enca_bench_readers_stop, 0);
  for (unsigned r = 0; r < readers; r++)
    {
      args[r].ops = ops;
      args[r].ring = ring;
      args[r].ring_len = ring_len;
      args[r].bytes_scanned = 0;
      /* One extra reference per reader: scans cannot race eviction. */
      for (enca_usize i = 0; i < ring_len; i++)
        if (ring[i])
          ops->retain (ring[i]);
      enca_thread_create (&thr[r], "bench-reader", enca_bench_reader_main,
                          &args[r]);
    }

  enca_deadline dl = enca_deadline_from_now_ms (read_ms);
  while (enca_deadline_remaining_ns (dl) > 0)
    ;                             /* busy wait keeps timing simple */

  atomic_store (&enca_bench_readers_stop, 1);
  enca_u64 bytes = 0;
  for (unsigned r = 0; r < readers; r++)
    {
      enca_thread_join (&thr[r]);
      bytes += args[r].bytes_scanned;
      for (enca_usize i = 0; i < ring_len; i++)
        if (ring[i])
          ops->release (ring[i]);
    }
  free (args);
  free (thr);

  return (double) bytes / ((double) read_ms / 1000.0) / (1024.0 * 1024.0);
}

#endif
