#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "cache.h"

#include "../memory/memory.h"

#include <string.h>

/* ---------------- key hashing ---------------- */

static enca_u64
ctkey_mix (enca_u64 h, enca_u64 v)
{
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static enca_u64
ctkey_hash (const enca_ct_cache_key *k)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  h = ctkey_mix (h, (enca_u64) k->document_id);
  h = ctkey_mix (h, k->revision);
  h = ctkey_mix (h, (enca_u64) k->cursor);
  h = ctkey_mix (h, (enca_u64) k->trigger);
  h = ctkey_mix (h, k->prefix_hash);
  return h;
}

static bool
ctkey_eq (const enca_ct_cache_key *a, const enca_ct_cache_key *b)
{
  return a->document_id == b->document_id && a->revision == b->revision
         && a->cursor == b->cursor && a->trigger == b->trigger
         && a->prefix_hash == b->prefix_hash;
}

/* ---------------- entry ---------------- */

typedef struct ct_entry
{
  struct ct_entry *lru_prev, *lru_next; /* MRU at head               */
  struct ct_entry *hash_next;
  enca_ct_cache_key key;
  enca_u64 hash;
  enca_ct_model model;          /* immutable once inserted           */
} ct_entry;

struct enca_ct_cache
{
  ct_entry **buckets;
  size_t bucket_count;
  size_t max_entries;
  ct_entry *lru_head, *lru_tail; /* head = MRU                       */
  enca_ct_cache_stats st;
};

enca_result
enca_ct_cache_create (size_t max_entries, enca_ct_cache **out)
{
  if (!out || max_entries == 0)
    return ENCA_ERR_INVALID_ARGUMENT;
  enca_ct_cache *c = enca_malloc (sizeof *c);
  if (!c)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (c, 0, sizeof *c);
  c->max_entries = max_entries;
  c->bucket_count = max_entries * 2 + 1;
  c->buckets = enca_malloc (c->bucket_count * sizeof (ct_entry *));
  if (!c->buckets)
    {
      enca_free (c);
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  memset (c->buckets, 0, c->bucket_count * sizeof (ct_entry *));
  *out = c;
  return ENCA_OK;
}

static void
entry_payload_free (ct_entry *e)
{
  for (size_t i = 0; i < e->model.count; i++)
    {
      enca_free (e->model.items[i].label);
      enca_free (e->model.items[i].annot);
    }
  enca_free (e->model.items);
}

static void
lru_unlink (enca_ct_cache *c, ct_entry *e)
{
  if (e->lru_prev)
    e->lru_prev->lru_next = e->lru_next;
  else
    c->lru_head = e->lru_next;
  if (e->lru_next)
    e->lru_next->lru_prev = e->lru_prev;
  else
    c->lru_tail = e->lru_prev;
  e->lru_prev = e->lru_next = NULL;
}

static void
lru_push_mru (enca_ct_cache *c, ct_entry *e)
{
  e->lru_prev = NULL;
  e->lru_next = c->lru_head;
  if (c->lru_head)
    c->lru_head->lru_prev = e;
  c->lru_head = e;
  if (!c->lru_tail)
    c->lru_tail = e;
}

static void
bucket_remove (enca_ct_cache *c, ct_entry *e)
{
  ct_entry **pp = &c->buckets[e->hash % c->bucket_count];
  while (*pp && *pp != e)
    pp = &(*pp)->hash_next;
  if (*pp)
    *pp = e->hash_next;
}

/* Drop one entry completely (payload + links + accounting). */
static void
drop_entry (enca_ct_cache *c, ct_entry *e)
{
  lru_unlink (c, e);
  bucket_remove (c, e);
  c->st.entries--;
  c->st.bytes -= e->model.bytes;
  entry_payload_free (e);
  enca_free (e);
}

void
enca_ct_cache_destroy (enca_ct_cache *c)
{
  if (!c)
    return;
  ct_entry *e = c->lru_head;
  while (e)
    {
      ct_entry *next = e->lru_next;
      entry_payload_free (e);
      enca_free (e);
      e = next;
    }
  enca_free (c->buckets);
  enca_free (c);
}

void
enca_ct_cache_clear (enca_ct_cache *c)
{
  if (!c)
    return;
  ct_entry *e = c->lru_head;
  while (e)
    {
      ct_entry *next = e->lru_next;
      entry_payload_free (e);
      enca_free (e);
      e = next;
    }
  memset (c->buckets, 0, c->bucket_count * sizeof (ct_entry *));
  c->lru_head = c->lru_tail = NULL;
  c->st.entries = 0;
  c->st.bytes = 0;
}

enca_usize
enca_ct_cache_invalidate_document (enca_ct_cache *c,
                                   enca_object_id document_id)
{
  enca_usize removed = 0;
  ct_entry *e = c->lru_head;
  while (e)
    {
      ct_entry *next = e->lru_next;
      if (e->key.document_id == document_id)
        {
          drop_entry (c, e);
          c->st.invalidations++;
          removed++;
        }
      e = next;
    }
  return removed;
}

enca_result
enca_ct_cache_insert (enca_ct_cache *c, const enca_ct_cache_key *key,
                      enca_ct_model m)
{
  if (!c || !key)
    return ENCA_ERR_INVALID_ARGUMENT;

  const enca_u64 h = ctkey_hash (key);
  ct_entry *e = c->buckets[h % c->bucket_count];
  while (e)
    {
      if (e->hash == h && ctkey_eq (&e->key, key))
        {
          /* replace: drop old payload, keep position refreshed */
          entry_payload_free (e);
          c->st.bytes -= e->model.bytes;
          c->st.evictions++;
          e->model = m;
          c->st.bytes += m.bytes;
          lru_unlink (c, e);
          lru_push_mru (c, e);
          return ENCA_OK;
        }
      e = e->hash_next;
    }

  /* capacity: evict LRU tail first */
  while (c->st.entries >= c->max_entries)
    {
      if (!c->lru_tail)
        break;
      ct_entry *victim = c->lru_tail;
      drop_entry (c, victim);
      c->st.evictions++;
    }

  e = enca_malloc (sizeof *e);
  if (!e)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (e, 0, sizeof *e);
  e->key = *key;
  e->hash = h;
  e->model = m;
  size_t slot = h % c->bucket_count;
  e->hash_next = c->buckets[slot];
  c->buckets[slot] = e;
  lru_push_mru (c, e);
  c->st.entries++;
  c->st.bytes += m.bytes;
  return ENCA_OK;
}

bool
enca_ct_cache_lookup (enca_ct_cache *c, const enca_ct_cache_key *key,
                      const enca_ct_model **out)
{
  if (!c || !key || !out)
    return false;
  c->st.lookups++;

  const enca_u64 h = ctkey_hash (key);
  ct_entry *e = c->buckets[h % c->bucket_count];
  while (e)
    {
      if (e->hash == h && ctkey_eq (&e->key, key))
        {
          c->st.hits++;
          lru_unlink (c, e);
          lru_push_mru (c, e);
          *out = &e->model;
          return true;
        }
      e = e->hash_next;
    }
  c->st.misses++;
  return false;
}

void
enca_ct_cache_stats_get (const enca_ct_cache *c,
                         enca_ct_cache_stats *out)
{
  if (!out)
    return;
  if (c)
    *out = c->st;
  else
    memset (out, 0, sizeof *out);
}

/* ---------------- prefix-extension filter (Phase B helper) ------- */

enca_usize
enca_ct_filter_prefix (const enca_ct_model *m, const char *prefix,
                       size_t prefix_len, enca_ct_label_view *out,
                       size_t out_cap)
{
  if (!m || !m->count)
    return 0;
  enca_usize matched = 0;
  for (size_t i = 0; i < m->count; i++)
    {
      const char *lb = m->items[i].label;
      const size_t ll = m->items[i].label_len;
      if (ll < prefix_len)
        continue;
      if (prefix_len && memcmp (lb, prefix, prefix_len) != 0)
        continue;
      if (matched < out_cap)
        {
          out[matched].label = lb;      /* borrowed view, zero copy */
          out[matched].label_len = ll;
        }
      matched++;
    }
  return matched;
}
