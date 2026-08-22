/* Flat store: the P2.0 baseline cost model.  Every publish copies
   the whole document; bytes_copied/edit == resulting document size,
   independent of edit size. */

#include "store.h"

typedef struct
{
  enca_bench_store base;
} flat_store;

typedef struct
{
  enca_bench_rev base;
  _Atomic enca_u32 refs;
  unsigned char *data;
  enca_usize len;
} flat_rev;

static enca_result
flat_create (enca_bench_store **out, enca_usize chunk_size)
{
  (void) chunk_size;
  flat_store *st = enca_malloc (sizeof *st);
  if (!st)
    return ENCA_ERR_OUT_OF_MEMORY;
  st->base.ops = &enca_store_flat_ops;
  *out = &st->base;
  return ENCA_OK;
}

static enca_result
flat_snapshot_init (enca_bench_store *bst, const unsigned char *init,
                    enca_usize n, enca_bench_rev **out,
                    enca_rev_metrics *m)
{
  (void) bst;
  flat_rev *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  r->base.ops = &enca_store_flat_ops;
  atomic_store (&r->refs, 1);
  r->data = enca_malloc (n ? n : 1);
  if (!r->data)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  if (n)
    memcpy (r->data, init, n);
  r->len = n;

  m->content_copy_bytes = n;
  m->meta_bytes = sizeof *r;

  *out = &r->base;
  return ENCA_OK;
}

static enca_result
flat_publish (enca_bench_store *bst, enca_bench_rev *prev,
              const enca_edit_rec *e, enca_bench_rev **out,
              enca_rev_metrics *m)
{
  (void) bst;
  flat_rev *p = (flat_rev *) prev;

  enca_u64 pos = e->position > p->len ? p->len : e->position;
  enca_u64 del = e->delete_len;
  if (del > p->len - pos)
    del = p->len - pos;
  enca_usize newlen = p->len - (enca_usize) del
                      + (enca_usize) e->insert_len;

  flat_rev *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  r->base.ops = &enca_store_flat_ops;
  atomic_store (&r->refs, 1);
  r->data = enca_malloc (newlen ? newlen : 1);
  if (!r->data)
    {
      enca_free (r);
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  r->len = newlen;

  /* Single pass: prefix, skip deleted range, insert, suffix. */
  memcpy (r->data, p->data, pos);
  memcpy (r->data + pos + e->insert_len, p->data + pos + del,
          p->len - pos - del);
  if (e->insert_len)
    memcpy (r->data + pos, e->insert_data, e->insert_len);

  m->content_copy_bytes = newlen;
  m->meta_bytes = sizeof *r;

  *out = &r->base;
  return ENCA_OK;
}

static void
flat_retain (enca_bench_rev *rev)
{
  flat_rev *r = (flat_rev *) rev;
  atomic_fetch_add (&r->refs, 1);
}

static void
flat_release (enca_bench_rev *rev)
{
  flat_rev *r = (flat_rev *) rev;
  if (atomic_fetch_sub (&r->refs, 1) == 1)
    {
      enca_free (r->data);
      enca_free (r);
    }
}

static enca_u64
flat_fnv (enca_bench_rev *rev)
{
  flat_rev *r = (flat_rev *) rev;
  return enca_fnv1a (r->data, r->len);
}

static enca_u64
flat_read_random (enca_bench_rev *rev, const enca_u64 *offsets,
                  enca_usize n)
{
  flat_rev *r = (flat_rev *) rev;
  enca_u64 acc = 0;
  for (enca_usize i = 0; i < n; i++)
    acc += r->data[offsets[i] % (r->len ? r->len : 1)];
  return acc;
}

static enca_usize
flat_rev_len (enca_bench_rev *rev)
{
  return ((flat_rev *) rev)->len;
}

static void
flat_dump (enca_bench_rev *rev)
{
  (void) rev;
}

static void
flat_destroy (enca_bench_store *st)
{
  enca_free (st);
}

const enca_store_ops enca_store_flat_ops = {
  "flat",
  flat_create,
  flat_snapshot_init,
  flat_publish,
  flat_retain,
  flat_release,
  flat_fnv,
  flat_read_random,
  flat_rev_len,
  flat_dump,
  flat_destroy,
};
