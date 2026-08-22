#ifndef ENCA_SNAPSHOT_H
#define ENCA_SNAPSHOT_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../id/id.h"
#include "../thread/thread.h"

#include <stdatomic.h>

/* P2 Snapshot / State Isolation -- native layer.
   Contracts: ../ARCHITECTURE.md #15-#19, SNAPSHOT.md section 12. */

typedef enum enca_encoding
{
  ENCA_ENC_NONE = 0,
  ENCA_ENC_UTF8 = 1,
  ENCA_ENC_BINARY = 2,
  ENCA_ENC_OTHER = 3
} enca_encoding_t;

/* Canonical consumer view.  Hides storage representation (flat in
   P2.0; chunked later must not change this type). */
typedef struct enca_utf8_view
{
  const unsigned char *data;
  enca_usize len;
} enca_utf8_view;

/* Two distinct freshness levels; never merged into one scalar
   (ARCHITECTURE.md #19). */
typedef struct enca_snapshot_epoch
{
  enca_u64 runtime_generation;
  enca_u64 document_revision;
} enca_snapshot_epoch;

typedef struct enca_document enca_document;
typedef struct enca_document_snapshot enca_document_snapshot;
typedef struct enca_runtime enca_runtime;

typedef struct enca_snapshot_system
{
  enca_id_registry *registry;      /* borrowed; publishing thread only */

  _Atomic enca_u64 created;
  _Atomic enca_u64 published;
  _Atomic enca_u64 acquired;
  _Atomic enca_u64 released;
  _Atomic enca_u64 destroyed;

  _Atomic enca_u64 live_snapshots; /* cross-check: created - destroyed */

  /* Two-phase destruction (SNAPSHOT.md L5): when the last reference
     drops on ANY thread, the snapshot is pushed here atomically.
     Only enca_snap_reclaim (publishing thread) pops it and releases
     the registry slot.  Guarantees no other thread ever touches the
     registry or takes a lock. */
  _Atomic (enca_document_snapshot *) pending_reclaim;
} enca_snapshot_system;

struct enca_document
{
  enca_object_id self_id;          /* ENCA_OBJ_BUFFER slot              */
  _Atomic enca_u64 revision;       /* latest published revision         */
  enca_mutex publish_lock;         /* guards latest swap                */
  enca_document_snapshot *latest;  /* publisher-owned ref, may be NULL  */
  enca_snapshot_system *sys;
};

struct enca_document_snapshot
{
  _Atomic enca_u32 refs;

  enca_object_id self_id;          /* ENCA_OBJ_SNAPSHOT slot            */
  enca_object_id document_id;      /* value copy; no document pointer   */
  enca_snapshot_epoch epoch;       /* immutable after capture           */
  enca_encoding_t source_encoding;
  enca_flags_t flags;
  enca_u64 capture_ns;
  enca_usize source_len;           /* raw metadata only in P2.0         */

  enca_utf8_view text;             /* canonical UTF-8, owned flat copy  */

  enca_snapshot_system *sys;       /* for release-side bookkeeping      */

  struct enca_document_snapshot *pending_next; /* reclaim-stack link    */
};

/* Bind the subsystem to a registry (borrowed; caller keeps it alive
   until every snapshot and document is gone). */
enca_result enca_snap_init (enca_snapshot_system *sys,
                            enca_id_registry *registry);

/* Reclaim snapshots whose last reference dropped on other threads:
   releases their registry slots and memory.  MUST run on the
   publishing thread (same thread as publish / document APIs).
   Cheap no-op when the pending stack is empty; call it whenever
   accurate stats or registry liveness is about to be observed. */
void enca_snap_reclaim (enca_snapshot_system *sys);

typedef struct enca_snap_stats
{
  enca_u64 created;
  enca_u64 published;
  enca_u64 acquired;
  enca_u64 released;
  enca_u64 destroyed;
  enca_u64 live;                   /* tracked counter                   */
  enca_u64 live_computed;          /* created - destroyed               */
} enca_snap_stats;

void enca_snap_stats_get (const enca_snapshot_system *sys,
                          enca_snap_stats *out);

/* Capture input: bytes are assumed already canonical when encoding
   is UTF-8; transcoding is a future Capture-Adapter concern. */
typedef struct enca_capture_input
{
  enca_encoding_t encoding;
  const void *bytes;
  enca_usize len;
} enca_capture_input;

enca_result enca_document_create (enca_snapshot_system *sys,
                                  enca_document **out_doc);

/* Releases the publisher reference of any latest snapshot.  Live
   snapshots of this document stay valid (ARCHITECTURE.md #18). */
void enca_document_destroy (enca_document *doc);

ENCA_INLINE enca_u64
enca_document_revision (const enca_document *doc)
{
  return doc ? atomic_load_explicit (
    (const _Atomic enca_u64 *) &doc->revision, memory_order_acquire) : 0;
}

/* Publish one revision: capture + own an immutable copy + registry
   identity + slot swap.  Returns +1 reference via *out (L2); the
   publisher-slot reference is separate and released on supersede. */
ENCA_NODISCARD enca_result
enca_snapshot_publish (enca_snapshot_system *sys, enca_document *doc,
                       const enca_capture_input *in,
                       enca_u64 runtime_generation,
                       enca_document_snapshot **out);

/* Pure ownership: refcount increment only (SNAPSHOT.md L1).  NULL
   propagates. */
enca_document_snapshot *enca_snapshot_acquire (enca_document_snapshot *s);

void enca_snapshot_release (enca_document_snapshot *s);

/* Acquire the current latest revision, or NULL if none published. */
enca_document_snapshot *enca_document_latest_acquire (enca_document *doc);

ENCA_INLINE enca_utf8_view
enca_snapshot_text (const enca_document_snapshot *s)
{
  return s->text;
}

ENCA_INLINE const enca_snapshot_epoch *
enca_snapshot_epoch_of (const enca_document_snapshot *s)
{
  return &s->epoch;
}

ENCA_INLINE enca_object_id
enca_snapshot_identity (const enca_document_snapshot *s)
{
  return s ? s->self_id : ENCA_INVALID_ID;
}

/* Commit-side two-level validation (#19): the epoch matches iff the
   runtime generation still equals the result's generation AND the
   document's latest revision still equals the result's revision. */
bool enca_snapshot_epoch_current (const enca_document *doc,
                                  enca_u64 runtime_generation,
                                  const enca_snapshot_epoch *epoch);

/* Vertical-slice bridge: acquire-latest, submit a zero-copy borrow
   task whose payload destruction releases the transferred reference
   on every disposal path (runtime routes all paths through
   enca_task_input_destroy). */
ENCA_NODISCARD enca_result enca_snap_submit_latest (enca_document *doc,
                                                    enca_runtime *rt);

#endif /* ENCA_SNAPSHOT_H */
