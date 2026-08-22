/* P2.1 storage-study harness: runs one cell of the experiment
   matrix (store family x workload x size x edits x retention) and
   prints a CSV row plus human-readable percentiles.

   Correctness guard: the same deterministic edit script is applied
   to a flat reference buffer; store checksums are verified against
   it every --verify-every revisions and at the end. */

#include "metrics.h"
#include "workloads.h"

#include <stdio.h>

static void
usage (void)
{
  printf ("usage: bench_enca --store flat|chunked [--chunk N]\n"
          "                 --workload W1|W2|W3|W4|W5\n"
          "                 --size N --edits N [--retention K]\n"
          "                 [--readers R] [--read-ms MS]\n"
          "                 [--verify-every M] [--seed N]\n"
          "                 [--cslabel S] [--label S]\n");
}

int
main (int argc, char **argv)
{
  const char *family = "flat";
  const char *wl_name = "W1";
  const char *cs_label = "-";
  const char *label = "";
  const char *coalesce = "none";
  double frag_threshold = 2.0;
  enca_usize maint_every = 0;
  enca_u64 cold_hold_at = 0, cold_read_at = 0;
  (void) cold_read_at; /* reserved: delayed read scheduling */
  enca_u64 edit_size = 0;
  const char *locality = "append";
  enca_usize chunk_size = 65536;
  enca_usize doc_size = 1u << 20;
  enca_usize n_edits = 200;
  enca_usize retention = 4;
  unsigned readers = 0;
  unsigned read_ms = 300;
  unsigned verify_every = 8;
  enca_u64 seed = 42;

  for (int i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "--store") && i + 1 < argc)
        family = argv[++i];
      else if (!strcmp (argv[i], "--workload") && i + 1 < argc)
        wl_name = argv[++i];
      else if (!strcmp (argv[i], "--cslabel") && i + 1 < argc)
        cs_label = argv[++i];
      else if (!strcmp (argv[i], "--label") && i + 1 < argc)
        label = argv[++i];
      else if (!strcmp (argv[i], "--chunk") && i + 1 < argc)
        chunk_size = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--size") && i + 1 < argc)
        doc_size = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--edits") && i + 1 < argc)
        n_edits = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--retention") && i + 1 < argc)
        retention = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--readers") && i + 1 < argc)
        readers = (unsigned) atoi (argv[++i]);
      else if (!strcmp (argv[i], "--read-ms") && i + 1 < argc)
        read_ms = (unsigned) atoi (argv[++i]);
      else if (!strcmp (argv[i], "--verify-every") && i + 1 < argc)
        verify_every = (unsigned) atoi (argv[++i]);
      else if (!strcmp (argv[i], "--seed") && i + 1 < argc)
        seed = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--coalesce") && i + 1 < argc)
        coalesce = argv[++i];
      else if (!strcmp (argv[i], "--frag-threshold") && i + 1 < argc)
        frag_threshold = atof (argv[++i]);
      else if (!strcmp (argv[i], "--maint-every") && i + 1 < argc)
        maint_every = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--cold-hold-at") && i + 1 < argc)
        cold_hold_at = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--cold-read-at") && i + 1 < argc)
        cold_read_at = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--edit-size") && i + 1 < argc)
        edit_size = strtoull (argv[++i], NULL, 0);
      else if (!strcmp (argv[i], "--locality") && i + 1 < argc)
        locality = argv[++i];
      else
        {
          usage ();
          return 2;
        }
    }

  enca_workload_kind kind;
  if (!strcmp (wl_name, "W1"))
    kind = ENCA_WL_CODE_EDIT;
  else if (!strcmp (wl_name, "W2"))
    kind = ENCA_WL_TYPING;
  else if (!strcmp (wl_name, "W3"))
    kind = ENCA_WL_PASTE;
  else if (!strcmp (wl_name, "W4"))
    kind = ENCA_WL_REFACTOR;
  else if (!strcmp (wl_name, "W5"))
    kind = ENCA_WL_BIGFILE_LOCAL;
  else if (!strcmp (wl_name, "W6"))
    kind = ENCA_WL_SYNTHETIC;
  else
    {
      usage ();
      return 2;
    }

  int coalesce_mode = 0;
  if (!strcmp (family, "chunked"))
    {
      if (!strcmp (coalesce, "local"))
        coalesce_mode = 1;
      else if (!strcmp (coalesce, "deferred"))
        coalesce_mode = 2;
      enca_store_chunked_configure (coalesce_mode, frag_threshold);
    }

  const enca_store_ops *ops
    = !strcmp (family, "flat") ? &enca_store_flat_ops
                               : &enca_store_chunked_ops;

  unsigned char *ref = malloc (doc_size ? doc_size : 1);
  if (!ref)
    return 1;
  enca_usize ref_cap = doc_size ? doc_size : 1;
  {
    enca_prng p;
    enca_prng_seed (&p, seed ^ 0x1234);
    for (enca_usize i = 0; i < doc_size; i++)
      ref[i] = (unsigned char) enca_prng_next (&p);
  }
  enca_mem_stats mem0 = enca_mem_stats_snapshot ();

  enca_bench_store *st = NULL;
  enca_rev_metrics m;
  enca_bench_rev *cur = NULL;
  if (ops->create (&st, chunk_size) != ENCA_OK)
    return 1;
  if (ops->snapshot_init (st, ref, doc_size, &cur, &m) != ENCA_OK)
    return 1;

  enca_workload wl;
  enca_workload_init (&wl, kind, seed);
  if (!wl.scratch)
    return 1;
  if (kind == ENCA_WL_SYNTHETIC)
    {
      int loc = ENCA_LOC_APPEND;
      if (!strcmp (locality, "middle"))
        loc = ENCA_LOC_MIDDLE;
      else if (!strcmp (locality, "random"))
        loc = ENCA_LOC_RANDOM;
      else if (!strcmp (locality, "hot"))
        loc = ENCA_LOC_HOT;
      enca_workload_configure_synth (&wl, edit_size, loc);
    }

  /* Cold-snapshot support: hold one revision aside at cold_hold_at. */
  enca_bench_rev *cold_rev = NULL;
  enca_u64 cold_fnv = 0, cold_len = 0;

  enca_u64 *lat = malloc ((n_edits ? n_edits : 1) * sizeof *lat);
  enca_bench_rev **ring = calloc (retention ? retention : 1,
                                  sizeof *ring);
  enca_usize ring_head = 0, ring_count = 0;
  enca_u64 copied_total = 0, meta_total = 0, maint_copied_total = 0;
  bool ok = true;

  for (enca_usize i = 0; i < n_edits; i++)
    {
      enca_edit_rec e;
      enca_usize cur_len = ops->rev_len (cur);
      enca_workload_next (&wl, cur_len, &e);

      /* The document GROWS: grow the reference buffer before apply
         (worst case next length = cur_len + insert_len). */
      if (cur_len + e.insert_len > ref_cap)
        {
          while (ref_cap < cur_len + e.insert_len)
            ref_cap = ref_cap ? ref_cap * 2 : 4096;
          ref = realloc (ref, ref_cap);
          if (!ref)
            return 1;
        }
      enca_usize ref_len = enca_edit_apply (ref, cur_len, &e);

      enca_timestamp_ns t0 = enca_monotonic_now_ns ();
      enca_bench_rev *next = NULL;
      if (ops->publish (st, cur, &e, &next, &m) != ENCA_OK)
        {
          ok = false;
          break;
        }
      lat[i] = enca_monotonic_now_ns () - t0;
      if (getenv ("ENCA_BENCH_TRACE"))
        fprintf (stderr,
                 "[e%llu] pos=%llu del=%llu ins=%llu -> len=%zu b0=%02x/%02x\n",
                 (unsigned long long) i,
                 (unsigned long long) e.position,
                 (unsigned long long) e.delete_len,
                 (unsigned long long) e.insert_len,
                  ops->rev_len (next),
                  (unsigned) ops->read_random (cur ? next : next,
                                               &(enca_u64){ 0 }, 1),
                  ref[0]);
      copied_total += m.content_copy_bytes;
      meta_total += m.meta_bytes;

      /* Retention ring: one extra reference per held revision. */
      ops->retain (next);
      if (ring_count < retention)
        {
          ring[ring_head] = next;
          ring_head = (ring_head + 1) % (retention ? retention : 1);
          ring_count++;
        }
      else
        {
          ops->release (ring[ring_head]);
          ring[ring_head] = next;
          ring_head = (ring_head + 1) % retention;
        }

      ops->release (cur);       /* chain ref moves to `next` */
      cur = next;

      /* Cold snapshot: pin a revision at cold_hold_at, read it late. */
      if (cold_hold_at && i + 1 == cold_hold_at && !cold_rev)
        {
          cold_rev = cur;
          ops->retain (cold_rev);
          cold_fnv = ops->fnv_sequential (cold_rev);
          cold_len = ops->rev_len (cold_rev);
        }

      /* Deferred maintenance pass on the publishing thread. */
      if (maint_every && coalesce_mode == 2 && ops->maintain
          && (i + 1) % maint_every == 0)
        {
          enca_bench_rev *mt = NULL;
          enca_u64 mcopied = 0;
          if (ops->maintain (st, cur, &mt, &mcopied) == ENCA_OK && mt)
            {
              maint_copied_total += mcopied;
              ops->release (cur);
              cur = mt;         /* same content, compacted layout */
            }
        }

      if (verify_every && (i + 1) % verify_every == 0
          && ops->fnv_sequential (cur) != enca_fnv1a (ref, ref_len))
        {
          printf ("CORRUPT at edit %llu: "
                  "store(fnv=%llx len=%zu) ref(fnv=%llx len=%zu) "
                  "edit(pos=%llu del=%llu ins=%llu)\n",
                  (unsigned long long) i,
                  (unsigned long long) ops->fnv_sequential (cur),
                  ops->rev_len (cur),
                  (unsigned long long) enca_fnv1a (ref, ref_len), ref_len,
                  (unsigned long long) e.position,
                  (unsigned long long) e.delete_len,
                  (unsigned long long) e.insert_len);
          if (ops->dump)
            ops->dump (cur);
          for (enca_usize o = 0; o < ref_len; o++)
            {
              unsigned char sb
                = (unsigned char) ops->read_random (cur, &o, 1);
              if (sb != ref[o])
                {
                  printf ("  first diff at %llu: store=%02x ref=%02x\n",
                          (unsigned long long) o, sb, ref[o]);
                  break;
                }
            }
          ok = false;
          break;
        }
    }

  if (ok && ops->fnv_sequential (cur)
             != enca_fnv1a (ref, ops->rev_len (cur)))
    {
      printf ("FINAL CORRUPT\n");
      ok = false;
    }

  double reader_mbs
    = ok ? enca_bench_reader_phase (ops, ring, ring_count, readers,
                                    read_ms)
         : 0;

  /* Cold snapshot read (after the loop: maximally "cold"). */
  double cold_read_ms = -1;
  int cold_ok = -1;
  if (cold_rev)
    {
      enca_timestamp_ns c0 = enca_monotonic_now_ns ();
      enca_u64 h = ops->fnv_sequential (cold_rev);
      cold_read_ms = (double) (enca_monotonic_now_ns () - c0) / 1e6;
      cold_ok = (h == cold_fnv) && ops->rev_len (cold_rev) == cold_len;
      ops->release (cold_rev);
      cold_rev = NULL;
      fprintf (stderr, "[cold] read=%.4f ms ok=%d\n", cold_read_ms,
               cold_ok);
    }

  enca_mem_stats mem1 = enca_mem_stats_snapshot ();
  enca_u64 live_delta = mem1.live_bytes - mem0.live_bytes;

  double p50 = enca_bench_pct_ms (lat, n_edits, 50);
  double p90 = enca_bench_pct_ms (lat, n_edits, 90);
  double p99 = enca_bench_pct_ms (lat, n_edits, 99);
  double pmax = enca_bench_pct_ms (lat, n_edits, 100);

  /* Sharing ratio inputs: logical = sum(len of live revisions:
     ring + cur); physical from the store. */
  enca_u64 logical = ops->rev_len (cur);
  for (enca_usize i = 0; i < ring_count; i++)
    if (ring[i])
      logical += ops->rev_len (ring[i]);
  enca_u64 physical
    = ops->physical_bytes ? ops->physical_bytes (st) : 0;
  double sharing = physical ? (double) logical / (double) physical : 0;
  enca_u64 final_hash
    = ok ? ops->fnv_sequential (cur) : 0;

  /* CSV row. */
  printf ("%s,%s,%s,%llu,%llu,%llu,%d,%d,"
          "%.4f,%.4f,%.4f,%.4f,"
          "%llu,%llu,%llu,%.1f,%s,"
          "%llx,%llu,%llu,%.2f,%llu,%d,%s,%s,%llu\n",
          family, cs_label, wl_name,
          (unsigned long long) doc_size,
          (unsigned long long) n_edits,
          (unsigned long long) retention,
          (int) readers, (int) ok,
          p50, p90, p99, pmax,
          (unsigned long long) copied_total,
          (unsigned long long) meta_total,
          (unsigned long long) live_delta,
          reader_mbs, label,
          (unsigned long long) final_hash,
          (unsigned long long) logical,
          (unsigned long long) physical,
          sharing,
          (unsigned long long) maint_copied_total,
          cold_ok,
          coalesce,
          (kind == ENCA_WL_SYNTHETIC ? locality : "-"),
          (unsigned long long) (kind == ENCA_WL_SYNTHETIC ? edit_size : 0));

  fprintf (stderr,
           "[%s/%s size=%llu edits=%llu ret=%llu readers=%u]\n"
           "  capture ms p50=%.4f p90=%.4f p99=%.4f max=%.4f\n"
           "  copied=%llu meta=%llu live_delta=%llu reader=%0.f MB/s "
           "ok=%d\n"
           "  final=%llx logical=%llu physical=%llu sharing=%.2f "
           "maint_copied=%llu cold_ok=%d\n",
           family, enca_workload_name (kind),
           (unsigned long long) doc_size,
           (unsigned long long) n_edits,
           (unsigned long long) retention, readers, p50, p90, p99, pmax,
           (unsigned long long) copied_total,
           (unsigned long long) meta_total,
           (unsigned long long) live_delta, reader_mbs, (int) ok,
           (unsigned long long) final_hash,
           (unsigned long long) logical,
           (unsigned long long) physical, sharing,
           (unsigned long long) maint_copied_total, cold_ok);

  if (cold_rev)
    {
      ops->release (cold_rev);
      cold_rev = NULL;
    }
  /* Teardown: release ring + chain. */
  for (enca_usize i = 0; i < ring_count; i++)
    if (ring[i])
      ops->release (ring[i]);
  ops->release (cur);
  ops->destroy (st);
  enca_workload_destroy (&wl);
  free (ring);
  free (lat);
  free (ref);

  enca_mem_stats mem2 = enca_mem_stats_snapshot ();
  if (mem2.live_allocs != mem0.live_allocs)
    fprintf (stderr, "LEAK? live_allocs %lld -> %lld\n",
             (long long) mem0.live_allocs,
             (long long) mem2.live_allocs);

  return ok ? 0 : 1;
}
