/* Chunked store: first P2.1 candidate structure.

   Model: immutable chunk BUFFERS + per-revision piece tables.  A
   piece is a logical (buffer, offset, len) slice; an edit rewrites
   only the table around the touched range and allocates the fresh
   insert payload (split into <= chunk_size buffers).  Untouched
   slices keep referencing shared buffers, so bytes_copied/edit is
   the insert payload independent of document size, while old
   revisions stay valid as long as retained.

   Known v1 tradeoffs (measured, not hidden): pure slicing lets
   tiny-edit workloads grow piece counts -- sequential scan cost is
   part of the comparison.  Coalescing and rope/persistent-tree are
   deferred candidates. */

#include "store.h"

#include <stdio.h>

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

static enca_usize chunk_rev_len (enca_bench_rev *rev);

typedef struct
{
  enca_bench_store base;
  enca_usize chunk_size;
} chunk_store;

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
  if (n)
    memcpy (b->data, src, n);
  b->len = n;
  atomic_store (&b->refs, 1);
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
  st->base.ops = &enca_store_chunked_ops;
  st->chunk_size = chunk_size;
  *out = &st->base;
  return ENCA_OK;
}

/* Allocate a fresh table of N pieces. */
static enca_result
table_alloc (chunk_rev *r, enca_usize n)
{
  r->n_pieces = 0;
  r->bufs = n ? enca_malloc (n * sizeof *r->bufs) : NULL;
  r->offs = n ? enca_malloc (n * sizeof *r->offs) : NULL;
  r->lens = n ? enca_malloc (n * sizeof *r->lens) : NULL;
  if (n && (!r->bufs || !r->offs || !r->lens))
    return ENCA_ERR_OUT_OF_MEMORY;
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

  /* Split initial content into chunk-size buffers so later edits in
     different regions touch different shared buffers. */
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
      /* Direct assignment: takes buf_new's own reference. */
      r->bufs[i] = b;
      r->offs[i] = 0;
      r->lens[i] = (enca_u32) take;
      r->n_pieces = i + 1;
      left -= take;
    }

  m->content_copy_bytes = n;
  m->meta_bytes = nchunks * sizeof (chunk_buf)
                  + nchunks * (sizeof (chunk_buf *)
                               + sizeof (enca_u32) * 2);

  *out = &r->base;
  return ENCA_OK;
}

/* Piece index + local offset for absolute byte offset. */
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
  *idx = r->n_pieces;           /* one past last == append position */
  *local = 0;
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
  enca_u64 end = pos + del;     /* exclusive */

  enca_usize si, sj;
  enca_u32 so;
  locate (prev, pos, &si, &so);
  locate (prev, end, &sj, &(enca_u32){0});

  /* New table: prefix + head-slice + insert-pieces + tail-slice
     + suffix.  Worst case adds a handful of entries. */
  enca_usize max_entries = prev->n_pieces + 3
                           + e->insert_len / st->chunk_size + 2;
  if (table_alloc (r, max_entries) != ENCA_OK)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  enca_usize w = 0;

  enca_rev_metrics mm = { 0, 0 };

  /* 1. Prefix pieces before the touched one. */
  for (enca_usize i = 0; i < si && i < prev->n_pieces; i++)
    table_set (r, w++, prev->bufs[i], prev->offs[i], prev->lens[i]);

  /* 2. Head slice of the split start piece. */
  if (si < prev->n_pieces && so > 0)
    table_set (r, w++, prev->bufs[si], prev->offs[si], so);

  /* 3. Fresh insert payload split into chunk-size buffers. */
  {
    enca_usize left = e->insert_len;
    const unsigned char *src = e->insert_data;
    while (left > 0)
      {
        enca_usize take = left > st->chunk_size ? st->chunk_size : left;
        chunk_buf *b = buf_new (src, take);
        if (!b)
          goto oom;
        r->bufs[w] = b;         /* takes buf_new's own reference */
        r->offs[w] = 0;
        r->lens[w] = (enca_u32) take;
        w++;
        src += take;
        left -= take;
        mm.content_copy_bytes += take;
        mm.meta_bytes += sizeof (chunk_buf);
      }
  }

  /* 4. Tail slice of the split end piece (when bytes survive). */
  if (sj < prev->n_pieces)
    {
      enca_u64 acc_before_sj = 0;
      for (enca_usize k = 0; k < sj; k++)
        acc_before_sj += prev->lens[k];
      enca_u32 tail_off = (enca_u32) (end - acc_before_sj);
      enca_usize tail_len = prev->lens[sj] - tail_off;
      if (tail_off <= prev->lens[sj] && tail_len > 0)
        table_set (r, w++, prev->bufs[sj],
                   prev->offs[sj] + tail_off, (enca_u32) tail_len);
    }

  /* 5. Suffix pieces after the touched end piece. */
  for (enca_usize i = sj + 1; i < prev->n_pieces; i++)
    table_set (r, w++, prev->bufs[i], prev->offs[i], prev->lens[i]);

  r->n_pieces = w;
  mm.meta_bytes += max_entries * (sizeof (chunk_buf *)
                                  + 2 * sizeof (enca_u32));
  *m = mm;

  *out = &r->base;
  return ENCA_OK;

oom:
  table_free (r);
  enca_free (r);
  return ENCA_ERR_OUT_OF_MEMORY;
}

static void
chunk_retain (enca_bench_rev *rev)
{
  chunk_rev *r = (chunk_rev *) rev;
  atomic_fetch_add (&r->refs, 1);
}

/* Revision-level refcount: the table (and its piece references)
   dies exactly once, when the last holder drops. */
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

static void
chunk_destroy (enca_bench_store *st)
{
  enca_free (st);
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
  chunk_destroy,
};
