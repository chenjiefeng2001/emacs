#ifndef ENCA_CT_CACHE_H
#define ENCA_CT_CACHE_H

#include "completion.h"

/* EVS-5.2 Completion Result Cache prototype
   (bench/enca/evs5/CACHE.md).

   Bounded LRU over immutable candidate models, keyed by exact
   provenance {document_id, revision, cursor, trigger, prefix_hash}.
   Phase A discipline: every field must match -- prefix extension and
   cross-revision reuse are later stages with their own gates.

   The cache lives in the Completion Engine layer.  A hit path never
   reaches the scheduler or the LSP backend (gate C11 is structural).
   Snapshot references are never stored: entries carry provenance
   fields only. */

typedef struct enca_ct_cache enca_ct_cache;

typedef struct
{
  enca_object_id document_id;
  enca_u64 revision;
  size_t cursor;
  unsigned trigger;             /* enca_ct_trigger                   */
  enca_u64 prefix_hash;         /* FNV-1a of raw prefix bytes        */
} enca_ct_cache_key;

typedef struct
{
  enca_u64 lookups;
  enca_u64 hits;
  enca_u64 misses;
  enca_u64 evictions;
  enca_u64 invalidations;
  enca_usize entries;
  enca_usize bytes;
} enca_ct_cache_stats;

enca_result enca_ct_cache_create (size_t max_entries,
                                  enca_ct_cache **out);
void enca_ct_cache_destroy (enca_ct_cache *c);

/* Insert takes ownership of M (it becomes the entry payload).  On
   duplicate key the old entry is replaced (counted as eviction). */
enca_result enca_ct_cache_insert (enca_ct_cache *c,
                                  const enca_ct_cache_key *key,
                                  enca_ct_model m);

/* Exact-key lookup.  On hit, promotes to MRU and returns a BORROWED
   const view valid until destroy/clear/eviction of that entry. */
bool enca_ct_cache_lookup (enca_ct_cache *c,
                           const enca_ct_cache_key *key,
                           const enca_ct_model **out);

/* Drop every entry belonging to DOCUMENT_ID (document invalidated /
   closed).  Returns number removed. */
enca_usize enca_ct_cache_invalidate_document (enca_ct_cache *c,
                                              enca_object_id document_id);

void enca_ct_cache_clear (enca_ct_cache *c);

void enca_ct_cache_stats_get (const enca_ct_cache *c,
                              enca_ct_cache_stats *out);

/* Zero-copy prefix-extension filter (Phase B helper): views into M's
   labels whose bytes start with PREFIX.  Returns matched count; OUT
   receives at most OUT_CAP views. */
typedef struct
{
  const char *label;
  size_t label_len;
} enca_ct_label_view;

enca_usize enca_ct_filter_prefix (const enca_ct_model *m,
                                  const char *prefix, size_t prefix_len,
                                  enca_ct_label_view *out,
                                  size_t out_cap);

#endif /* ENCA_CT_CACHE_H */
