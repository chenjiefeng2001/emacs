#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "snapshot.h"

#include "../base/assert.h"
#include "../memory/memory.h"
#include "../time/time.h"
#include "../runtime/runtime.h"

#include <string.h>

/* ---------------------------------------------------------------- */
/* Subsystem lifecycle                                              */

enca_result
enca_snap_init (enca_snapshot_system *sys, enca_id_registry *registry)
{
  if (!sys || !registry)
    return ENCA_ERR_INVALID_ARGUMENT;

  memset (sys, 0, sizeof *sys);
  sys->registry = registry;
  return ENCA_OK;
}

void
enca_snap_reclaim (enca_snapshot_system *sys)
{
  if (!sys)
    return;

  /* Single-consumer pop: only the publishing thread runs this. */
  for (;;)
    {
      enca_document_snapshot *s = atomic_load_explicit (
        &sys->pending_reclaim, memory_order_acquire);
      if (!s)
        break;
      enca_document_snapshot *next = s->pending_next;
      if (atomic_compare_exchange_strong_explicit (&sys->pending_reclaim,
                                                   &s, next,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire))
        {
          enca_result r = enca_idr_free (sys->registry, s->self_id);
          ENCA_ASSERT_ALWAYS (r == ENCA_OK, "snapshot registry slot leak");
          if (s->istorage_release)
            s->istorage_release (s->istorage);
          else
            enca_free ((void *) s->text.data);
          enca_free (s);
          atomic_fetch_add_explicit (&sys->destroyed, 1,
                                     memory_order_relaxed);
        }
    }
}

void
enca_snap_stats_get (const enca_snapshot_system *sys, enca_snap_stats *out)
{
  if (!out)
    return;
  memset (out, 0, sizeof *out);
  if (!sys)
    return;

  out->created = atomic_load_explicit (&sys->created, memory_order_relaxed);
  out->published
    = atomic_load_explicit (&sys->published, memory_order_relaxed);
  out->acquired = atomic_load_explicit (&sys->acquired, memory_order_relaxed);
  out->released = atomic_load_explicit (&sys->released, memory_order_relaxed);
  out->destroyed
    = atomic_load_explicit (&sys->destroyed, memory_order_relaxed);
  out->live = atomic_load_explicit (&sys->live_snapshots,
                                    memory_order_relaxed);
  out->live_computed = out->created - out->destroyed;
}

/* ---------------------------------------------------------------- */
/* Snapshot internals                                               */

/* Runs on whichever thread drops the last reference: worker, reader
   or main.  Registry and heap-free work are deferred to the
   publishing thread via the pending stack (L5); everything here is
   wait-free atomics. */
static void
snap_retire (enca_document_snapshot *s)
{
  enca_snapshot_system *sys = s->sys;

  for (;;)
    {
      enca_document_snapshot *head = atomic_load_explicit (
        &sys->pending_reclaim, memory_order_relaxed);
      s->pending_next = head;
      if (atomic_compare_exchange_strong_explicit (&sys->pending_reclaim,
                                                   &head, s,
                                                   memory_order_release,
                                                   memory_order_relaxed))
        break;
    }

  atomic_fetch_sub_explicit (&sys->live_snapshots, 1, memory_order_acq_rel);
}

enca_document_snapshot *
enca_snapshot_acquire (enca_document_snapshot *s)
{
  if (!s)
    return NULL;
  atomic_fetch_add_explicit (&s->refs, 1, memory_order_acq_rel);
  atomic_fetch_add_explicit (&s->sys->acquired, 1, memory_order_relaxed);
  return s;
}

void
enca_snapshot_release (enca_document_snapshot *s)
{
  if (!s)
    return;

  atomic_fetch_add_explicit (&s->sys->released, 1, memory_order_relaxed);

  enca_u32 prev = atomic_fetch_sub_explicit (&s->refs, 1,
                                             memory_order_acq_rel);
  if (prev == 1)
    snap_retire (s);
}

/* ---------------------------------------------------------------- */
/* Document lifecycle                                               */

enca_result
enca_document_create (enca_snapshot_system *sys, enca_document **out_doc)
{
  if (!sys || !out_doc)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_document *doc = enca_malloc (sizeof *doc);
  if (!doc)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (doc, 0, sizeof *doc);

  enca_result r = enca_mutex_init (&doc->publish_lock);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_free (doc);
      return r;
    }

  r = enca_idr_alloc (sys->registry, ENCA_OBJ_BUFFER, &doc->self_id);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_mutex_destroy (&doc->publish_lock);
      enca_free (doc);
      return r;
    }

  doc->sys = sys;
  *out_doc = doc;
  return ENCA_OK;
}

void
enca_document_destroy (enca_document *doc)
{
  if (!doc)
    return;

  enca_mutex_lock (&doc->publish_lock);
  enca_document_snapshot *old = doc->latest;
  doc->latest = NULL;
  enca_mutex_unlock (&doc->publish_lock);

  /* May run while workers still hold older snapshots: snapshot
     lifetime is independent from document lifetime (#18). */
  enca_snapshot_release (old);

  enca_result r = enca_idr_free (doc->sys->registry, doc->self_id);
  ENCA_ASSERT_ALWAYS (r == ENCA_OK, "document registry slot leak");

  enca_mutex_destroy (&doc->publish_lock);
  enca_free (doc);
}

/* ---------------------------------------------------------------- */
/* Publication                                                      */

enca_result
enca_snapshot_publish (enca_snapshot_system *sys, enca_document *doc,
                       const enca_capture_input *in,
                       enca_u64 runtime_generation,
                       enca_document_snapshot **out)
{
  if (!sys || !doc || !in || !out || (in->len > 0 && !in->bytes))
    return ENCA_ERR_INVALID_ARGUMENT;

  /* Natural publishing-thread sweep point for deferred reclaims. */
  enca_snap_reclaim (sys);

  enca_document_snapshot *s = enca_malloc (sizeof *s);
  if (!s)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (s, 0, sizeof *s);

  s->text.len = in->len;
  s->text.data = NULL;
  if (in->len > 0)
    {
      unsigned char *copy = enca_malloc (in->len);
      if (!copy)
        {
          enca_free (s);
          return ENCA_ERR_OUT_OF_MEMORY;
        }
      memcpy (copy, in->bytes, in->len);
      s->text.data = copy;
    }

  enca_result r = enca_idr_alloc (sys->registry, ENCA_OBJ_SNAPSHOT,
                                  &s->self_id);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_free ((void *) s->text.data);
      enca_free (s);
      return r;
    }

  /* Publisher-slot reference. */
  atomic_store_explicit (&s->refs, 1, memory_order_relaxed);
  s->document_id = doc->self_id;
  s->source_encoding = in->encoding;
  s->capture_ns = enca_monotonic_now_ns ();
  s->source_len = in->len;
  s->sys = sys;

  enca_mutex_lock (&doc->publish_lock);
  s->epoch.runtime_generation = runtime_generation;
  s->epoch.document_revision
    = atomic_load_explicit (&doc->revision, memory_order_relaxed) + 1;
  atomic_store_explicit (&doc->revision, s->epoch.document_revision,
                         memory_order_release);
  enca_document_snapshot *old = doc->latest;
  doc->latest = s;
  enca_mutex_unlock (&doc->publish_lock);

  /* Outside the lock: destruction takes no document locks. */
  enca_snapshot_release (old);

  atomic_fetch_add_explicit (&sys->created, 1, memory_order_relaxed);
  atomic_fetch_add_explicit (&sys->published, 1, memory_order_relaxed);
  atomic_fetch_add_explicit (&sys->live_snapshots, 1, memory_order_acq_rel);

  /* Hand the caller its own reference (L2). */
  enca_snapshot_acquire (s);
  *out = s;
  return ENCA_OK;
}

enca_document_snapshot *
enca_document_latest_acquire (enca_document *doc)
{
  if (!doc)
    return NULL;

  enca_mutex_lock (&doc->publish_lock);
  enca_document_snapshot *s = doc->latest ? enca_snapshot_acquire (
    doc->latest) : NULL;
  enca_mutex_unlock (&doc->publish_lock);
  return s;
}

void
enca_document_adopt_snapshot (enca_document *doc,
                              enca_document_snapshot *snap)
{
  if (!doc || !snap)
    return;

  enca_mutex_lock (&doc->publish_lock);
  atomic_store_explicit (&doc->revision, snap->epoch.document_revision,
                         memory_order_release);
  enca_document_snapshot *old = doc->latest;
  doc->latest = snap;           /* caller's reference becomes the slot */
  enca_mutex_unlock (&doc->publish_lock);

  /* Outside the lock: destruction takes no document locks. */
  enca_snapshot_release (old);
}

/* ---------------------------------------------------------------- */
/* Commit validation                                                */

bool
enca_snapshot_epoch_current (const enca_document *doc,
                             enca_u64 runtime_generation,
                             const enca_snapshot_epoch *epoch)
{
  if (!doc || !epoch)
    return false;
  return epoch->runtime_generation == runtime_generation
         && epoch->document_revision
              == atomic_load_explicit (&doc->revision,
                                       memory_order_acquire);
}

/* ---------------------------------------------------------------- */
/* Vertical-slice bridge                                            */

static void
snap_input_destroy_hook (enca_task_input *ti)
{
  enca_document_snapshot *s = ti->user_data;
  enca_snapshot_release (s);
}

enca_result
enca_snap_submit_latest (enca_document *doc, enca_runtime *rt)
{
  if (!doc || !rt)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_document_snapshot *s = enca_document_latest_acquire (doc);
  if (!s)
    return ENCA_ERR_NOT_FOUND;

  enca_task_submit req;
  req.source_id = doc->self_id;
  req.flags = ENCA_TASK_BORROW_INPUT;   /* view borrowed, ref retained */
  req.data = s->text.data;
  req.n = s->text.len;
  req.stream_revision = s->epoch.document_revision;
  req.user_data = s;                    /* reference transferred       */
  req.input_destroy = snap_input_destroy_hook;

  enca_result r = enca_runtime_submit_ex (rt, &req);
  if (ENCA_RESULT_IS_ERR (r))
    enca_snapshot_release (s);
  return r;
}

/* ---------------- EVS-2.2: incremental document state ---------------- */

/* Piece-backed storage.  Piece buffers are immutable and refcounted;
   each revision owns a refcounted table of (buf, off, len) slices.
   Tables share piece buffers across revisions -- an edit rewrites only
   the touched range of the table and allocates the fresh insert
   payload.  No coalescing in this phase; fragmentation is measurable
   via enca_doc_state_piece_count / avg_piece_size.

   Ownership: the doc state holds one reference to the latest
   revision's table; every published snapshot holds its own reference
   to its revision's table.  Superseding an edit drops only the doc
   state's reference (#18/#EVS-2.2.4).

   External snapshot semantics are identical to the flat path: callers
   use acquire/release/epoch/revision and read text via
   enca_snapshot_walk_text.  Storage kind never leaks (#EVS-2.2.1). */

typedef struct
{
  _Atomic unsigned refs;
  unsigned char *data;
  size_t len;
} ist_piece;

static void
ist_piece_ref (ist_piece *p)
{
  atomic_fetch_add_explicit (&p->refs, 1, memory_order_acq_rel);
}

static void
ist_piece_unref (ist_piece *p)
{
  if (atomic_fetch_sub_explicit (&p->refs, 1, memory_order_acq_rel) == 1)
    {
      enca_free (p->data);
      enca_free (p);
    }
}

typedef struct
{
  ist_piece *buf;
  size_t off;
  size_t len;
} ist_slice;

typedef struct
{
  _Atomic unsigned refs;
  ist_slice *slices;
  size_t n;
} ist_table;

struct enca_doc_state
{
  enca_snapshot_system *sys;
  enca_document *doc;

  ist_table *table;             /* latest revision; one reference     */
  size_t total_len;
  enca_u64 revision;            /* monotonic; first edit -> 1         */
  enca_u64 alloc_bytes;         /* payload bytes copied into pieces   */
};

static ist_table *
ist_table_alloc (size_t cap)
{
  ist_table *t = enca_malloc (sizeof *t);
  if (!t)
    return NULL;
  memset (t, 0, sizeof *t);
  atomic_init (&t->refs, 1);
  t->slices = cap ? enca_malloc (cap * sizeof *t->slices) : NULL;
  if (cap && !t->slices)
    {
      enca_free (t);
      return NULL;
    }
  return t;
}

static void
ist_table_ref (ist_table *t)
{
  atomic_fetch_add_explicit (&t->refs, 1, memory_order_acq_rel);
}

static void
ist_table_unref (ist_table *t)
{
  if (!t)
    return;
  if (atomic_fetch_sub_explicit (&t->refs, 1, memory_order_acq_rel) != 1)
    return;
  for (size_t i = 0; i < t->n; i++)
    ist_piece_unref (t->slices[i].buf);
  enca_free (t->slices);
  enca_free (t);
}

static void
ist_table_set (ist_table *t, size_t i, ist_piece *b, size_t off,
               size_t len)
{
  ist_piece_ref (b);
  t->slices[i].buf = b;
  t->slices[i].off = off;
  t->slices[i].len = len;
  if (i + 1 > t->n)
    t->n = i + 1;
}

/* Snapshot-side storage release (runs whenever the last reference of
   an incremental snapshot drops). */
static void
ist_release_storage_cb (void *storage)
{
  ist_table_unref (storage);
}

static ist_piece *
ist_piece_new (const unsigned char *src, size_t n)
{
  ist_piece *p = enca_malloc (sizeof *p);
  if (!p)
    return NULL;
  p->data = enca_malloc (n ? n : 1);
  if (!p->data)
    {
      enca_free (p);
      return NULL;
    }
  if (n)
    memcpy (p->data, src, n);
  p->len = n;
  atomic_init (&p->refs, 1);
  return p;
}

enca_usize
enca_doc_state_length (const enca_doc_state *ds)
{
  return ds ? ds->total_len : 0;
}

enca_usize
enca_doc_state_piece_count (const enca_doc_state *ds)
{
  return (ds && ds->table) ? ds->table->n : 0;
}

double
enca_doc_state_avg_piece_size (const enca_doc_state *ds)
{
  return (ds && ds->table && ds->table->n)
           ? (double) ds->total_len / (double) ds->table->n
           : 0.0;
}

enca_usize
enca_doc_state_alloc_bytes (const enca_doc_state *ds)
{
  return ds ? ds->alloc_bytes : 0;
}

enca_result
enca_doc_state_create (enca_snapshot_system *sys, enca_document *doc,
                       enca_doc_state **out)
{
  if (!sys || !doc || !out)
    return ENCA_ERR_INVALID_ARGUMENT;
  enca_doc_state *ds = enca_malloc (sizeof *ds);
  if (!ds)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (ds, 0, sizeof *ds);
  ds->sys = sys;
  ds->doc = doc;
  *out = ds;
  return ENCA_OK;
}

void
enca_doc_state_destroy (enca_doc_state *ds)
{
  if (!ds)
    return;
  ist_table_unref (ds->table);
  enca_free (ds);
}

/* Locate the piece containing absolute byte offset OFF.
   Past-the-end maps to append position.  LOCAL may be NULL. */
static void
ist_locate (const ist_table *t, size_t off, size_t *idx, size_t *local)
{
  size_t acc = 0;
  for (size_t i = 0; i < t->n; i++)
    {
      if (off < acc + t->slices[i].len)
        {
          *idx = i;
          if (local)
            *local = off - acc;
          return;
        }
      acc += t->slices[i].len;
    }
  *idx = t->n;
  if (local)
    *local = 0;
}

/* Publish a new revision from the builder-owned table T (the caller
   transfers its reference).  On success the snapshot owns T and the
   doc state adopts its own reference; on failure T stays with the
   caller.  OUT may be NULL: fire-and-forget publication (the creation
   reference is dropped immediately; the snapshot becomes reclaimable
   on the next publishing-thread sweep). */
static enca_result
ist_publish_snapshot (enca_doc_state *ds, ist_table *t, size_t total_len,
                      enca_document_snapshot **out)
{
  enca_document_snapshot *snap = enca_malloc (sizeof *snap);
  if (!snap)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (snap, 0, sizeof *snap);
  atomic_init (&snap->refs, 1);
  snap->sys = ds->sys;
  snap->document_id = ds->doc ? ds->doc->self_id : ENCA_INVALID_ID;
  snap->source_encoding = ENCA_ENC_UTF8;
  snap->capture_ns = enca_monotonic_now_ns ();
  snap->source_len = total_len;
  snap->text.data = NULL;       /* piece-backed: access via walk_text */
  snap->text.len = total_len;

  /* Identity through the standard registry (same as flat path). */
  enca_result r = enca_idr_alloc (ds->sys->registry, ENCA_OBJ_SNAPSHOT,
                                  &snap->self_id);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_free (snap);
      return r;
    }

  snap->istorage = t;           /* snapshot adopts the builder ref    */
  snap->istorage_release = ist_release_storage_cb;

  ds->revision++;
  snap->epoch.document_revision = ds->revision;
  ds->total_len = total_len;

  /* Doc state takes its own reference to the new root table; the old
     root stays alive for snapshots of earlier revisions. */
  ist_table_ref (t);
  ist_table *old_root = ds->table;
  ds->table = t;
  ist_table_unref (old_root);

  atomic_fetch_add_explicit (&ds->sys->created, 1, memory_order_relaxed);
  atomic_fetch_add_explicit (&ds->sys->published, 1,
                             memory_order_relaxed);
  atomic_fetch_add_explicit (&ds->sys->live_snapshots, 1,
                             memory_order_acq_rel);

  /* There is no publisher slot on this path: the creation reference
     IS the caller reference when OUT is given, otherwise it is
     dropped immediately so the snapshot enters the normal
     ownership/reclamation path (#EVS-2.4 fire-and-forget rule). */
  if (out)
    *out = snap;
  else
    enca_snapshot_release (snap);
  return ENCA_OK;
}

enca_result
enca_doc_state_edit (enca_doc_state *ds, size_t start_byte,
                     size_t del_len, const void *ins_data,
                     size_t ins_len, enca_document_snapshot **out)
{
  if (!ds || (ins_len > 0 && !ins_data))
    return ENCA_ERR_INVALID_ARGUMENT;

  /* Natural publishing-thread sweep point for deferred reclaims. */
  enca_snap_reclaim (ds->sys);

  const ist_table *ot = ds->table;
  const size_t old_len = ds->total_len;

  /* Memmove-style clamping (#EVS-2.2 canonical byte offsets). */
  const size_t pos = start_byte > old_len ? old_len : start_byte;
  const size_t del = del_len > old_len - pos ? old_len - pos : del_len;
  const size_t end = pos + del;
  const size_t new_len = old_len - del + ins_len;

  ist_table *nt;
  if (!ot || ot->n == 0)
    {
      /* First content on an empty document: one fresh payload piece
         (or an empty revision for an empty edit). */
      nt = ist_table_alloc (1);
      if (!nt)
        return ENCA_ERR_OUT_OF_MEMORY;
      if (ins_len > 0)
        {
          ist_piece *b = ist_piece_new (ins_data, ins_len);
          if (!b)
            {
              ist_table_unref (nt);
              return ENCA_ERR_OUT_OF_MEMORY;
            }
          ds->alloc_bytes += ins_len;
          nt->slices[0].buf = b;  /* table owns the creation ref      */
          nt->slices[0].off = 0;
          nt->slices[0].len = ins_len;
          nt->n = 1;
        }
    }
  else
    {
      size_t si, so;
      ist_locate (ot, pos, &si, &so);
      size_t sj;
      ist_locate (ot, end, &sj, NULL);

      /* Worst case grows the table by both split edges + insert. */
      nt = ist_table_alloc (ot->n + 3);
      if (!nt)
        return ENCA_ERR_OUT_OF_MEMORY;
      size_t w = 0;

      /* 1. Prefix pieces before the touched start piece (shared). */
      for (size_t i = 0; i < si; i++)
        ist_table_set (nt, w++, ot->slices[i].buf, ot->slices[i].off,
                       ot->slices[i].len);

      /* 2. Head slice of the split start piece (shared, zero copy). */
      if (si < ot->n && so > 0)
        ist_table_set (nt, w++, ot->slices[si].buf, ot->slices[si].off,
                       so);

      /* 3. Fresh insert payload: one ENCA-owned piece (copied once,
         #EVS-2.2.3). */
      if (ins_len > 0)
        {
          ist_piece *b = ist_piece_new (ins_data, ins_len);
          if (!b)
            {
              ist_table_unref (nt);
              return ENCA_ERR_OUT_OF_MEMORY;
            }
          ds->alloc_bytes += ins_len;
          nt->slices[w].buf = b;  /* table owns the creation ref      */
          nt->slices[w].off = 0;
          nt->slices[w].len = ins_len;
          nt->n = ++w;
        }

      /* 4. Tail slice of the split end piece (shared, zero copy).
         Fully-deleted middle pieces are simply not carried over;
         their buffers die with the dropped references. */
      if (sj < ot->n)
        {
          size_t acc_before_sj = 0;
          for (size_t k = 0; k < sj; k++)
            acc_before_sj += ot->slices[k].len;
          size_t tail_off = end - acc_before_sj;
          if (tail_off < ot->slices[sj].len)
            ist_table_set (nt, w++, ot->slices[sj].buf,
                           ot->slices[sj].off + tail_off,
                           ot->slices[sj].len - tail_off);
        }

      /* 5. Suffix pieces after the touched end piece (shared refs). */
      for (size_t i = sj + 1; i < ot->n; i++)
        ist_table_set (nt, w++, ot->slices[i].buf, ot->slices[i].off,
                       ot->slices[i].len);

      nt->n = w;
    }

  enca_result r = ist_publish_snapshot (ds, nt, new_len, out);
  if (ENCA_RESULT_IS_ERR (r))
    ist_table_unref (nt);       /* builder reference only            */
  return r;
}

void
enca_snapshot_walk_text (const enca_document_snapshot *s,
                         enca_text_walk_fn fn, void *ctx)
{
  if (!s || !fn)
    return;

  if (s->istorage)
    {
      const ist_table *t = s->istorage;
      for (size_t i = 0; i < t->n; i++)
        {
          size_t len = t->slices[i].len;
          if (len == 0)
            continue;           /* empty revisions carry no piece    */
          if (!fn (t->slices[i].buf->data + t->slices[i].off, len, ctx))
            return;
        }
    }
  else if (s->text.len > 0)
    fn (s->text.data, s->text.len, ctx);
}


