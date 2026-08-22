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
