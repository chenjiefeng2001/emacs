/* Chunked store: first P2.1 candidate structure.

   Model: immutable chunk BUFFERS + per-revision piece tables.  A
   piece is a logical (buffer, offset, len) slice; an edit rewrites
   only the table around the touched range and allocates the fresh
   insert payload (split into <= chunk_size buffers).

   Coalescing strategies (P2.1.5):
     C0 none      -- pure slicing, pieces accumulate (v1 behaviour)
     C1 local     -- eager: merge the edited run when <= chunk_size
                     (bounded write amplification per edit)
     C2 deferred  -- maintenance() merges fragmented runs on the
                     publishing thread when invoked by the harness;
                     foreground publish stays cheap

   Deferred matches the ENCA async philosophy: foreground path is
   latency-critical, maintenance is amortized. */

#include "store.h"

#include <stdio.h>

typedef enum
{
  COALESCE_NONE = 0,
  COALESCE_LOCAL = 1,
  COALESCE_DEFERRED = 2,
} enca_coalesce_mode;

static void chunk_dump (enca_bench_rev *rev);
static enca_usize chunk_rev_len (enca_bench_rev *rev);

typedef struct
{
  _Atomic enca_u32 refs;
  unsigned char *data;
  enca_usize len;
} chunk_buf;

typedef struct
{
  enca_bench_rev base;
  _Atomic enca_u32 refs;
  chunk_buf **bufs;
  enca_u32 *offs;
  enca_u32 *lens;
  enca_usize n_pieces;
} chunk_rev;

typedef struct
{
  enca_bench_store base;
  enca_usize chunk_size;
  enca_coalesce_mode mode;
  double frag_threshold;
  _Atomic enca_u64 buf_bytes_live;
  _Atomic enca_u64 table_bytes_live;
  _Atomic enca_u64 maint_copied;
} chunk_store;

static chunk_store *g_cst;    /* single store per process (bench) */

static int g_cfg_mode;
static double g_cfg_thr = 2.0;

void
enca_store_chunked_configure (int mode, double frag_threshold)
{
  g_cfg_mode = mode;
  g_cfg_thr = frag_threshold > 0 ? frag_threshold : 2.0;
}

static chunk_buf *
buf_new (const unsigned char *src, enca_usize n)
{
  chunk_buf *b = enca_malloc (sizeof *b);
  if (!b)
    return NULL;
  b->data = enca_malloc (n ? n : 1);
  if (!b->data)
    {
      enca_free (b);
      return NULL;
    }
  /* src == NULL: caller fills the buffer manually (merge paths). */
  if (n && src)
    memcpy (b->data, src, n);
  b->len = n;
  atomic_store (&b->refs, 1);
  atomic_fetch_add (&g_cst->buf_bytes_live,
                    n + sizeof (chunk_buf));
  return b;
}

static void
buf_ref (chunk_buf *b)
{
  atomic_fetch_add (&b->refs, 1);
}

static void
buf_unref (chunk_buf *b)
{
  if (atomic_fetch_sub (&b->refs, 1) == 1)
    {
      atomic_fetch_sub (&g_cst->buf_bytes_live,
                        b->len + sizeof (chunk_buf));
      enca_free (b->data);
      enca_free (b);
    }
}

static enca_result
chunk_create (enca_bench_store **out, enca_usize chunk_size)
{
  if (chunk_size == 0)
    chunk_size = 65536;
  chunk_store *st = enca_malloc (sizeof *st);
  if (!st)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (st, 0, sizeof *st);
  st->base.ops = &enca_store_chunked_ops;
  st->chunk_size = chunk_size;
  st->mode = (enca_coalesce_mode) g_cfg_mode;
  st->frag_threshold = g_cfg_thr;
  g_cst = st;
  *out = &st->base;
  return ENCA_OK;
}

static enca_result
table_alloc (chunk_rev *r, enca_usize n)
{
  r->n_pieces = 0;
  r->bufs = n ? enca_malloc (n * sizeof *r->bufs) : NULL;
  r->offs = n ? enca_malloc (n * sizeof *r->offs) : NULL;
  r->lens = n ? enca_malloc (n * sizeof *r->lens) : NULL;
  if (n && (!r->bufs || !r->offs || !r->lens))
    return ENCA_ERR_OUT_OF_MEMORY;
  atomic_fetch_add (&g_cst->table_bytes_live,
                    n * (sizeof (chunk_buf *) + 2 * sizeof (enca_u32)));
  return ENCA_OK;
}

static void
table_set (chunk_rev *r, enca_usize i, chunk_buf *b, enca_u32 off,
           enca_u32 len)
{
  buf_ref (b);
  r->bufs[i] = b;
  r->offs[i] = off;
  r->lens[i] = len;
  if (i + 1 > r->n_pieces)
    r->n_pieces = i + 1;
}

static void
table_free (chunk_rev *r)
{
  for (enca_usize i = 0; i < r->n_pieces; i++)
    buf_unref (r->bufs[i]);
  atomic_fetch_sub (&g_cst->table_bytes_live,
                    r->n_pieces * (sizeof (chunk_buf *)
                                   + 2 * sizeof (enca_u32)));
  enca_free (r->bufs);
  enca_free (r->offs);
  enca_free (r->lens);
  r->bufs = NULL;
  r->offs = NULL;
  r->lens = NULL;
  r->n_pieces = 0;
}

static enca_result
chunk_snapshot_init (enca_bench_store *bst, const unsigned char *init,
                     enca_usize n, enca_bench_rev **out,
                     enca_rev_metrics *m)
{
  chunk_store *st = (chunk_store *) bst;
  chunk_rev *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (r, 0, sizeof *r);
  r->base.ops = &enca_store_chunked_ops;
  atomic_store (&r->refs, 1);

  enca_usize nchunks = (n + st->chunk_size - 1) / st->chunk_size;
  if (nchunks == 0)
    nchunks = 1;

  if (table_alloc (r, nchunks) != ENCA_OK)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }

  enca_usize left = n;
  for (enca_usize i = 0; i < nchunks; i++)
    {
      enca_usize take = left > st->chunk_size ? st->chunk_size : left;
      chunk_buf *b = buf_new (left ? init + (n - left) : init, take);
      if (!b)
        {
          table_free (r);
          enca_free (r);
          return ENCA_ERR_OUT_OF_MEMORY;
        }
      r->bufs[i] = b;
      r->offs[i] = 0;
      r->lens[i] = (enca_u32) take;
      r->n_pieces = i + 1;
      left -= take;
    }

  m->content_copy_bytes = n;
  m->meta_bytes = 0;

  *out = &r->base;
  return ENCA_OK;
}

static void
locate (const chunk_rev *r, enca_u64 off, enca_usize *idx,
        enca_u32 *local)
{
  enca_u64 acc = 0;
  for (enca_usize i = 0; i < r->n_pieces; i++)
    {
      if (off < acc + r->lens[i])
        {
          *idx = i;
          *local = (enca_u32) (off - acc);
          return;
        }
      acc += r->lens[i];
    }
  *idx = r->n_pieces;
  *local = 0;
}

/* Merge a contiguous RUN of table entries [from, to) whose combined
   length is <= chunk_size into one fresh buffer. */
static enca_result
merge_run (chunk_rev *r, enca_usize from, enca_usize to,
           enca_rev_metrics *mm)
{
  enca_usize total = 0;
  for (enca_usize i = from; i < to; i++)
    total += r->lens[i];

  chunk_buf *b = buf_new (NULL, total);
  if (!b)
    return ENCA_ERR_OUT_OF_MEMORY;
  enca_usize o = 0;
  for (enca_usize i = from; i < to; i++)
    {
      memcpy (b->data + o, r->bufs[i]->data + r->offs[i], r->lens[i]);
      o += r->lens[i];
    }

  /* Drop old refs, keep ours from buf_new. */
  for (enca_usize i = from; i < to; i++)
    buf_unref (r->bufs[i]);
  r->bufs[from] = b;
  r->offs[from] = 0;
  r->lens[from] = (enca_u32) total;

  /* Compact entries after the merged run. */
  for (enca_usize i = to; i < r->n_pieces; i++)
    {
      r->bufs[from + 1 + (i - to)] = r->bufs[i];
      r->offs[from + 1 + (i - to)] = r->offs[i];
      r->lens[from + 1 + (i - to)] = r->lens[i];
    }
  r->n_pieces -= (to - from - 1);

  mm->content_copy_bytes += total;
  return ENCA_OK;
}

static enca_result
chunk_publish (enca_bench_store *bst, enca_bench_rev *prev_base,
               const enca_edit_rec *e, enca_bench_rev **out,
               enca_rev_metrics *m)
{
  chunk_store *st = (chunk_store *) bst;
  const chunk_rev *prev = (const chunk_rev *) prev_base;

  if (!prev)
    return ENCA_ERR_INVALID_ARGUMENT;

  chunk_rev *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (r, 0, sizeof *r);
  r->base.ops = &enca_store_chunked_ops;
  atomic_store (&r->refs, 1);

  enca_u64 total = 0;
  for (enca_usize i = 0; i < prev->n_pieces; i++)
    total += prev->lens[i];

  enca_u64 pos = e->position > total ? total : e->position;
  enca_u64 del = e->delete_len;
  if (del > total - pos)
    del = total - pos;
  enca_u64 end = pos + del;

  enca_usize si, sj;
  enca_u32 so;
  locate (prev, pos, &si, &so);
  locate (prev, end, &sj, &(enca_u32){ 0 });

  enca_usize max_entries = prev->n_pieces + 3
                           + e->insert_len / st->chunk_size + 2;
  if (table_alloc (r, max_entries) != ENCA_OK)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  enca_usize w = 0;

  enca_rev_metrics mm = { 0, 0 };

  /* 1. Prefix. */
  for (enca_usize i = 0; i < si && i < prev->n_pieces; i++)
    table_set (r, w++, prev->bufs[i], prev->offs[i], prev->lens[i]);

  /* 2. Head slice. */
  if (si < prev->n_pieces && so > 0)
    table_set (r, w++, prev->bufs[si], prev->offs[si], so);

  /* Record where the edited run begins (for C1). */
  enca_usize run_start = w;

  /* 3. Fresh insert payload. */
  {
    enca_usize left = e->insert_len;
    const unsigned char *src = e->insert_data;
    while (left > 0)
      {
        enca_usize take = left > st->chunk_size ? st->chunk_size : left;
        chunk_buf *b = buf_new (src, take);
        if (!b)
          goto oom;
        r->bufs[w] = b;
        r->offs[w] = 0;
        r->lens[w] = (enca_u32) take;
        w++;
        src += take;
        left -= take;
        mm.content_copy_bytes += take;
      }
  }


  /* 4. Tail slice. */
  if (sj < prev->n_pieces)
    {
      enca_u64 acc_before_sj = 0;
      for (enca_usize k = 0; k < sj; k++)
        acc_before_sj += prev->lens[k];
      enca_u32 tail_off = (enca_u32) (end - acc_before_sj);
      enca_usize tail_len = prev->lens[sj] - tail_off;
      if (tail_off <= prev->lens[sj] && tail_len > 0)
        {
          table_set (r, w++, prev->bufs[sj],
                     prev->offs[sj] + tail_off, (enca_u32) tail_len);
          if (0)
          {
            /* (kept for clarity: tail belongs to the edited run) */
          }
        }
    }

  /* The edited run spans [run_start .. run_end) including the tail
     slice when it came from the same neighbourhood. */
  enca_usize run_end = w;

  /* 5. Suffix. */
  for (enca_usize i = sj + 1; i < prev->n_pieces; i++)
    table_set (r, w++, prev->bufs[i], prev->offs[i], prev->lens[i]);

  r->n_pieces = w;

  /* C1 local eager coalescing: merge the edited run when it fits in
     one chunk.  Bounded write amplification: <= chunk_size per edit
     (only when a merge actually happens). */
  if (st->mode == COALESCE_LOCAL && run_end > run_start + 1)
    {
      enca_usize total_run = 0;
      for (enca_usize i = run_start; i < run_end; i++)
        total_run += r->lens[i];
      if (total_run > 0 && total_run <= st->chunk_size)
        {
          if (merge_run (r, run_start, run_end, &mm) != ENCA_OK)
            goto oom;
        }
    }

  *m = mm;
  *out = &r->base;
  return ENCA_OK;

oom:
  table_free (r);
  enca_free (r);
  return ENCA_ERR_OUT_OF_MEMORY;
}

/* C2: deferred maintenance -- merge adjacent fragments while the
   average piece length is below chunk_size / frag_threshold. */
static enca_result
chunk_maintain (enca_bench_store *bst, enca_bench_rev *cur_base,
                enca_bench_rev **out, enca_u64 *maint_copied)
{
  chunk_store *st = (chunk_store *) bst;
  const chunk_rev *cur = (const chunk_rev *) cur_base;

  if (st->mode != COALESCE_DEFERRED || cur->n_pieces == 0)
    {
      *out = NULL;
      return ENCA_OK;           /* nothing to do */
    }

  enca_u64 total = 0;
  for (enca_usize i = 0; i < cur->n_pieces; i++)
    total += cur->lens[i];
  double avg = (double) total / (double) cur->n_pieces;
  double ratio = (double) st->chunk_size / avg;
  if (ratio < st->frag_threshold)
    {
      *out = NULL;              /* not fragmented enough */
      return ENCA_OK;
    }

  chunk_rev *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (r, 0, sizeof *r);
  r->base.ops = &enca_store_chunked_ops;
  atomic_store (&r->refs, 1);
  if (table_alloc (r, cur->n_pieces) != ENCA_OK)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }

  enca_u64 copied = 0;
  enca_usize w = 0;
  enca_usize i = 0;
  while (i < cur->n_pieces)
    {
      /* Greedy: grow a run while it stays <= chunk_size. */
      enca_u64 run = cur->lens[i];
      enca_usize j = i + 1;
      while (j < cur->n_pieces && run + cur->lens[j] <= st->chunk_size)
        {
          run += cur->lens[j];
          j++;
        }
      if (j == i + 1)
        {
          /* Single piece fits by itself: share it. */
          table_set (r, w++, cur->bufs[i], cur->offs[i], cur->lens[i]);
          i++;
        }
      else
        {
          /* Copy the run into one fresh buffer. */
          chunk_buf *b = buf_new (NULL, run);
          if (!b)
            {
              table_free (r);
              enca_free (r);
              return ENCA_ERR_OUT_OF_MEMORY;
            }
          enca_usize o = 0;
          for (enca_usize k = i; k < j; k++)
            {
              memcpy (b->data + o, cur->bufs[k]->data + cur->offs[k],
                      cur->lens[k]);
              o += cur->lens[k];
            }
          r->bufs[w] = b;       /* takes buf_new's ref */
          r->offs[w] = 0;
          r->lens[w] = (enca_u32) run;
          w++;
          copied += run;
          i = j;
        }
    }
  r->n_pieces = w;
  *maint_copied = copied;
  atomic_fetch_add (&st->maint_copied, copied);
  *out = &r->base;
  return ENCA_OK;
}

static void
chunk_retain (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  atomic_fetch_add (&r->refs, 1);
}

static void
chunk_release (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  if (atomic_fetch_sub (&r->refs, 1) == 1)
    {
      table_free (r);
      enca_free (r);
    }
}

static enca_u64
chunk_fnv (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (enca_usize i = 0; i < r->n_pieces; i++)
    {
      const unsigned char *p = r->bufs[i]->data + r->offs[i];
      for (enca_usize k = 0; k < r->lens[i]; k++)
        {
          h ^= p[k];
          h *= (enca_u64) 1099511628211ull;
        }
    }
  return h;
}

static enca_u64
chunk_read_random (enca_bench_rev *rev, const enca_u64 *offsets,
                   enca_usize n)
{
  chunk_rev *r = (chunk_rev *) rev;
  enca_usize total = chunk_rev_len (&r->base);

  enca_u64 acc = 0;
  for (enca_usize q = 0; q < n; q++)
    {
      enca_u64 off = offsets[q] % (total ? total : 1);
      enca_u64 c = 0;
      for (enca_usize i = 0; i < r->n_pieces; i++)
        {
          if (off < c + r->lens[i])
            {
              acc += r->bufs[i]->data[r->offs[i]
                                      + (enca_usize) (off - c)];
              break;
            }
          c += r->lens[i];
        }
    }
  return acc;
}

static enca_usize
chunk_rev_len (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  enca_usize total = 0;
  for (enca_usize i = 0; i < r->n_pieces; i++)
    total += r->lens[i];
  return total;
}

static void
chunk_dump (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  enca_u64 acc = 0;
  for (enca_usize i = 0; i < r->n_pieces; i++)
    {
      fprintf (stderr,
               "  piece %3zu: buf=%p off=%u len=%u abs=%llu data0=%02x\n",
               i, (void *) r->bufs[i], r->offs[i], r->lens[i],
               (unsigned long long) acc,
               r->lens[i] ? r->bufs[i]->data[r->offs[i]] : 0);
      acc += r->lens[i];
    }
}

static enca_u64
chunk_physical_bytes (enca_bench_store *st)
{
  chunk_store *s = (chunk_store *) st;
  return atomic_load (&s->buf_bytes_live)
         + atomic_load (&s->table_bytes_live);
}

static void
chunk_destroy (enca_bench_store *st)
{
  enca_free (st);
  g_cst = NULL;
}

const enca_store_ops enca_store_chunked_ops = {
  "chunked",
  chunk_create,
  chunk_snapshot_init,
  chunk_publish,
  chunk_retain,
  chunk_release,
  chunk_fnv,
  chunk_read_random,
  chunk_rev_len,
  chunk_dump,
  chunk_physical_bytes,
  chunk_maintain,
  chunk_destroy,
};
