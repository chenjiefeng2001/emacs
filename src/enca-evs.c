/* enca-evs.c --- EVS-1: Emacs Interactive Vertical Slice.

Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* First real vertical slice: keypress -> buffer mutation -> capture
   -> snapshot -> admission -> schedule -> execute -> result -> commit
   validation, with per-revision latency accounting.

   Worker computes a deterministic synthetic analysis (FNV-1a over the
   snapshot text): no LSP, no network, no external processes, so the
   numbers isolate Emacs overhead + ENCA overhead.

   Commit rule: a result commits only when its document revision
   equals the CURRENT document revision at poll time.  Stale results
   are counted as wasted work and dropped.

   EVS-2.3: two interchangeable CAPTURE STRATEGIES behind the same
   pipeline (src/enca/snapshot/EVS2-DECISION.md section 1):
   - full:         buffer-string -> enca_snapshot_publish (EVS-1 arm A)
   - incremental:  byte-range edit -> enca_doc_state_edit ->
                   enca_document_adopt_snapshot     (arm B)
   The analysis worker reads text through enca_snapshot_walk_text,
   which serves BOTH storage kinds; nothing downstream changed.  */

#include <config.h>

#ifdef HAVE_ENCA_EVS

#include "lisp.h"

#include "enca/snapshot/snapshot.h"
#include "enca/scheduler/scheduler.h"
#include "enca/id/id.h"

#include <stdio.h>

#define EVS_RING 4096

typedef struct
{
  enca_u64 rev;
  enca_u64 submit_ns;
  enca_u64 commit_ns;
  enca_u64 value;
  bool committed;               /* false = dropped stale */
} evs_lat_rec;

static enca_id_registry evs_reg;
static enca_snapshot_system evs_sys;
static enca_document *evs_doc;
static enca_scheduler evs_sched;
static int evs_active;
static int evs_workers;

/* EVS-2.3 incremental capture (arm B).  EVS_DS is non-NULL exactly
   when the slice was started in incremental mode; it owns the
   piece-backed revision history and feeds the standard document slot
   via enca_document_adopt_snapshot. */
static enca_doc_state *evs_ds;
static _Atomic enca_u64 evs_delta_copied_total;  /* bytes into pieces  */
static _Atomic enca_u64 evs_delta_changed_total; /* logical del + ins  */

static _Atomic enca_u64 evs_committed_rev;
static _Atomic enca_u64 evs_submitted_total;
static _Atomic enca_u64 evs_executed_total;
static _Atomic enca_u64 evs_wasted_total;      /* stale commits */
static _Atomic enca_u64 evs_superseded_total;

/* Capture-phase attribution (EVS-1 closure): total ns spent inside
   capture+publish and how many captures were made.  Capture is the
   buffer-string copy + snapshot publish, measured on the main
   thread at submit time. */
static _Atomic enca_u64 evs_capture_ns_total;
static _Atomic enca_u64 evs_capture_count;

/* Latency ring (written from main thread inside commit callback). */
static evs_lat_rec evs_ring[EVS_RING];
static enca_usize evs_ring_pos;
static enca_usize evs_ring_fill;

/* Lisp-visible marker of the latest committed revision. */
static Lisp_Object evs_last_commit;



void syms_of_enca_evs (void);

static void
evs_release_snapshot (void *h)
{
  enca_snapshot_release ((enca_document_snapshot *) h);
}

/* Synthetic deterministic analysis: FNV-1a over the snapshot text.
   Stands in for parse/completion/diagnostics workloads.  Reads via
   the generic walk so flat and piece-backed snapshots share one code
   path (#EVS-2.2.1: storage kind never leaks). */
static bool
evs_fnv_walk (const unsigned char *data, size_t len, void *ctx)
{
  enca_u64 *h = ctx;
  for (size_t i = 0; i < len; i++)
    {
      *h ^= data[i];
      *h *= (enca_u64) 1099511628211ull;
    }
  return true;
}

static int
evs_exec_fn (const enca_sched_task *t, void *ctx, enca_u64 *out)
{
  enca_document_snapshot *snap = t->snapshot_handle;
  if (!snap)
    return -1;
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  enca_snapshot_walk_text (snap, evs_fnv_walk, &h);
  *out = h;
  return 0;
}

static void
evs_commit_cb (const enca_sched_result *r, void *ctx)
{
  enca_u64 now = enca_monotonic_now_ns ();
  bool stale = r->document_revision
                 != enca_document_revision ((enca_document *) ctx);
  bool good = !stale && r->status == ENCA_TSTAT_EXECUTED;

  if (good)
    {
      atomic_store (&evs_committed_rev, r->document_revision);
      atomic_fetch_add (&evs_executed_total, 1);

      evs_lat_rec *rec = &evs_ring[evs_ring_pos];
      rec->rev = r->document_revision;
      rec->submit_ns = r->submitted_ns;
      rec->commit_ns = now;
      rec->value = r->value;
      rec->committed = true;
      evs_ring_pos = (evs_ring_pos + 1) % EVS_RING;
      if (evs_ring_fill < EVS_RING)
        evs_ring_fill++;

      /* Visible marker for Elisp-side validation. */
      evs_last_commit = make_uint (r->document_revision);
    }
  else
    {
      atomic_fetch_add (&evs_wasted_total, 1);
      evs_lat_rec *rec = &evs_ring[evs_ring_pos];
      rec->rev = r->document_revision;
      rec->submit_ns = r->submitted_ns;
      rec->commit_ns = now;
      rec->value = 0;
      rec->committed = false;
      evs_ring_pos = (evs_ring_pos + 1) % EVS_RING;
      if (evs_ring_fill < EVS_RING)
        evs_ring_fill++;
    }
}

static void
evs_stop_internal (void)
{
  if (!evs_active)
    return;
  enca_sched_shutdown (&evs_sched);
  enca_sched_destroy (&evs_sched);
  enca_doc_state_destroy (evs_ds);
  evs_ds = NULL;
  enca_document_destroy (evs_doc);
  enca_snap_reclaim (&evs_sys);
  evs_doc = NULL;
  evs_active = 0;
}

DEFUN ("enca-evs-start", Fenca_evs_start, Senca_evs_start, 0, 2, 0,
       doc: /* Start the EVS slice with WORKERS background workers.
With optional INCREMENTAL non-nil, capture runs through the EVS-2
piece-backed document state (edit deltas) instead of full-buffer
publication; the pipeline beyond capture is identical.
Returns the number of workers actually started.  */)
  (Lisp_Object workers, Lisp_Object incremental)
{
  if (evs_active)
    error ("EVS already active");
  EMACS_INT n = 2;
  if (FIXNATP (workers))
    n = XFIXNAT (workers);
  if (n < 1)
    n = 1;
  if (n > 16)
    n = 16;

  memset (&evs_reg, 0, sizeof evs_reg);
  if (enca_idr_init (&evs_reg) != ENCA_OK)
    error ("EVS: registry init failed");
  if (enca_snap_init (&evs_sys, &evs_reg) != ENCA_OK)
    error ("EVS: snapshot system init failed");
  if (enca_document_create (&evs_sys, &evs_doc) != ENCA_OK)
    error ("EVS: document create failed");
  if (!NILP (incremental)
      && enca_doc_state_create (&evs_sys, evs_doc, &evs_ds) != ENCA_OK)
    error ("EVS: document state create failed");
  if (enca_sched_init (&evs_sched) != ENCA_OK)
    error ("EVS: scheduler init failed");

  atomic_store (&evs_committed_rev, 0);
  atomic_store (&evs_submitted_total, 0);
  atomic_store (&evs_executed_total, 0);
  atomic_store (&evs_wasted_total, 0);
  atomic_store (&evs_superseded_total, 0);
  atomic_store (&evs_delta_copied_total, 0);
  atomic_store (&evs_delta_changed_total, 0);
  evs_ring_pos = 0;
  evs_ring_fill = 0;
  evs_last_commit = Qnil;

  if (enca_sched_start_workers (&evs_sched, (unsigned) n,
                                evs_exec_fn, NULL) != ENCA_OK)
    {
      evs_stop_internal ();
      error ("EVS: worker start failed");
    }
  evs_active = 1;
  evs_workers = (int) n;
  return make_fixnum (n);
}

DEFUN ("enca-evs-stop", Fenca_evs_stop, Senca_evs_stop, 0, 0, 0,
       doc: /* Stop the EVS-1 slice and release all resources.  */)
  (void)
{
  evs_stop_internal ();
  return Qt;
}

/* Route one captured revision through admission.  SNAP is a
   reference the caller transfers; supersession releases it via the
   release hook. */
static enca_admit_result
evs_submit_snapshot_task (enca_document_snapshot *snap)
{
  enca_sched_task t;
  memset (&t, 0, sizeof t);
  t.document_id = evs_doc->self_id;
  t.cls = ENCA_TCLASS_INTERACTIVE;
  t.generation = 1;
  t.document_revision = enca_document_revision (evs_doc);
  t.snapshot_handle = snap;
  t.release_snapshot = evs_release_snapshot;
  t.urgency = ENCA_URGENCY_INTERACTIVE;
  t.deadline_ns = ENCA_DEADLINE_NONE;

  atomic_fetch_add (&evs_submitted_total, 1);
  enca_admit_result r = enca_sched_submit (&evs_sched, &t, NULL);
  if (r == ENCA_ADMIT_REPLACED)
    atomic_fetch_add (&evs_superseded_total, 1);
  return r;
}

/* Capture + submit: called from the after-change hook (or directly
   from benchmarks).  STRING is the full buffer content snapshot.
   Returns admission result; *capture_ns (if non-NULL) receives the
   capture+publish duration for latency attribution. */
static enca_admit_result
evs_capture_submit (const unsigned char *data, enca_usize len,
                    enca_u64 *capture_ns)
{
  enca_u64 t0 = enca_monotonic_now_ns ();
  enca_capture_input in = { ENCA_ENC_UTF8, data, len };
  enca_document_snapshot *snap = NULL;
  if (enca_snapshot_publish (&evs_sys, evs_doc, &in, 1, &snap) != ENCA_OK)
    return ENCA_ADMIT_REJECTED;
  enca_u64 t1 = enca_monotonic_now_ns ();
  if (capture_ns)
    *capture_ns = t1 - t0;
  atomic_fetch_add (&evs_capture_ns_total, t1 - t0);
  atomic_fetch_add (&evs_capture_count, 1);

  return evs_submit_snapshot_task (snap);       /* ref transferred   */
}

/* EVS-2.3 arm B: capture ONE contiguous byte-range edit and submit.
   BEG/DEL are canonical byte offsets into the PREVIOUS revision
   (deleted span [BEG, BEG+DEL)); INS replaces it and is copied once
   into ENCA-owned memory at this boundary (#EVS-2.2.3).  The new
   snapshot is adopted into the standard document slot so commit
   validation sees the same revision sequence as arm A. */
static enca_admit_result
evs_edit_submit (enca_usize beg, enca_usize del,
                 const unsigned char *ins, enca_usize ins_len,
                 enca_u64 *capture_ns)
{
  enca_u64 before = enca_doc_state_alloc_bytes (evs_ds);
  enca_u64 t0 = enca_monotonic_now_ns ();
  enca_document_snapshot *snap = NULL;
  if (enca_doc_state_edit (evs_ds, beg, del, ins, ins_len, &snap)
      != ENCA_OK)
    return ENCA_ADMIT_REJECTED;
  enca_document_adopt_snapshot (evs_doc, snap); /* ref -> slot       */
  enca_u64 t1 = enca_monotonic_now_ns ();
  if (capture_ns)
    *capture_ns = t1 - t0;
  atomic_fetch_add (&evs_capture_ns_total, t1 - t0);
  atomic_fetch_add (&evs_capture_count, 1);
  atomic_fetch_add (&evs_delta_copied_total,
                    enca_doc_state_alloc_bytes (evs_ds) - before);
  atomic_fetch_add (&evs_delta_changed_total, (enca_u64) del + ins_len);

  /* The slot holds the adopted reference; the task acquires its own
     so supersession disposal stays symmetric with arm A. */
  return evs_submit_snapshot_task (enca_snapshot_acquire (snap));
}

DEFUN ("enca-evs-on-change-delta", Fenca_evs_on_change_delta,
       Senca_evs_on_change_delta, 3, 3, 0,
       doc: /* Incremental capture (EVS-2): apply one contiguous edit
described by canonical BYTE offsets into the previous revision --
deleted span [BEG, END) -- with INS replacing it, then schedule
analysis.  Only valid when the slice was started in incremental mode.
Returns accepted/replaced/folded/expired as a symbol.  */)
  (Lisp_Object beg, Lisp_Object end, Lisp_Object ins)
{
  CHECK_FIXNAT (beg);
  CHECK_FIXNAT (end);
  CHECK_STRING (ins);
  if (!evs_active)
    error ("EVS not active");
  if (!evs_ds)
    error ("EVS not started in incremental mode");
  EMACS_INT b = XFIXNAT (beg), e = XFIXNAT (end);
  if (e < b)
    {
      EMACS_INT tmp = b; b = e; e = tmp;
    }
  enca_admit_result r = evs_edit_submit ((enca_usize) b,
                                         (enca_usize) (e - b),
                                         SDATA (ins),
                                         (enca_usize) SBYTES (ins),
                                         NULL);
  switch (r)
    {
    case ENCA_ADMIT_ACCEPTED: return Qaccepted;
    case ENCA_ADMIT_REPLACED: return Qreplaced;
    case ENCA_ADMIT_FOLDED: return Qfolded;
    default: return Qexpired;
    }
}

DEFUN ("enca-evs-on-change", Fenca_evs_on_change, Senca_evs_on_change, 1, 1, 0,
       doc: /* Capture STRING as a new document revision and schedule
analysis.  Intended for `after-change-functions'.  Returns
accepted/replaced/folded/expired as a symbol.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);
  if (!evs_active)
    error ("EVS-1 not active");
  enca_admit_result r = evs_capture_submit (SDATA (string),
                                            (enca_usize) SBYTES (string),
                                            NULL);
  switch (r)
    {
    case ENCA_ADMIT_ACCEPTED: return Qaccepted;
    case ENCA_ADMIT_REPLACED: return Qreplaced;
    case ENCA_ADMIT_FOLDED: return Qfolded;
    default: return Qexpired;
    }
}

/* B1 baseline: the SAME capture + synthetic analysis, executed
   synchronously on the calling thread with NO scheduler, NO worker,
   NO result routing.  Returns (HASH ELAPSED-MS).  */
DEFUN ("enca-evs-sync-analysis", Fenca_evs_sync_analysis,
       Senca_evs_sync_analysis, 1, 1, 0,
       doc: /* Run the EVS-1 synthetic analysis synchronously on STRING.
Returns (FNV-HASH ELAPSED-MS).  B1 baseline for the closure study.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);
  if (!evs_active)
    error ("EVS-1 not active");

  enca_u64 t0 = enca_monotonic_now_ns ();
  enca_capture_input in = { ENCA_ENC_UTF8, SDATA (string),
                            (enca_usize) SBYTES (string) };
  enca_document_snapshot *snap = NULL;
  if (enca_snapshot_publish (&evs_sys, evs_doc, &in, 1, &snap) != ENCA_OK)
    error ("EVS-1 sync publish failed");
  enca_utf8_view v = enca_snapshot_text (snap);
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (enca_usize i = 0; i < v.len; i++)
    {
      h ^= v.data[i];
      h *= (enca_u64) 1099511628211ull;
    }
  enca_u64 t1 = enca_monotonic_now_ns ();
  enca_snapshot_release (snap);
  return listn (2, make_uint (h),
                make_float ((double) (t1 - t0) / 1e6));
}

/* Attribution: (capture-total-ms capture-count avg-capture-ms). */
DEFUN ("enca-evs-capture-stats", Fenca_evs_capture_stats,
       Senca_evs_capture_stats, 0, 0, 0,
       doc: /* Capture-phase attribution accumulated since start.  */)
  (void)
{
  enca_u64 total = atomic_load (&evs_capture_ns_total);
  enca_u64 count = atomic_load (&evs_capture_count);
  double avg = count ? (double) total / (double) count / 1e6 : 0;
  return listn (3, make_float ((double) total / 1e6),
                make_uint (count), make_float (avg));
}

/* EVS-2.3: (copied-bytes changed-bytes edits) since start; zero
   unless started in incremental mode.  copy_amplification is
   copied/changed (EVS2.md section 7). */
DEFUN ("enca-evs-delta-stats", Fenca_evs_delta_stats,
       Senca_evs_delta_stats, 0, 0, 0,
       doc: /* Incremental-capture byte accounting since start.  */)
  (void)
{
  return listn (3,
                make_uint (atomic_load (&evs_delta_copied_total)),
                make_uint (atomic_load (&evs_delta_changed_total)),
                make_uint (atomic_load (&evs_capture_count)));
}

DEFUN ("enca-evs-pump", Fenca_evs_pump, Senca_evs_pump, 0, 0, 0,
       doc: /* Drain completed results and run commit validation.
Returns the number of results routed.  */)
  (void)
{
  if (!evs_active)
    return make_fixnum (0);
  return make_fixnum ((int) enca_sched_poll (&evs_sched,
                                             evs_commit_cb, evs_doc));
}

DEFUN ("enca-evs-last-commit", Fenca_evs_last_commit,
       Senca_evs_last_commit, 0, 0, 0,
       doc: /* Latest committed document revision, or nil.  */)
  (void)
{
  enca_u64 rev = atomic_load (&evs_committed_rev);
  return rev ? make_uint (rev) : Qnil;
}

DEFUN ("enca-evs-stats", Fenca_evs_stats, Senca_evs_stats, 0, 0, 0,
       doc: /* EVS-1 counters: (submitted committed wasted superseded
executed workers).  */)
  (void)
{
  return listn (6,
                make_uint (atomic_load (&evs_submitted_total)),
                make_uint (atomic_load (&evs_executed_total)),
                make_uint (atomic_load (&evs_wasted_total)),
                make_uint (atomic_load (&evs_superseded_total)),
                make_uint (atomic_load (&evs_committed_rev) ? 1 : 0),
                make_fixnum (evs_workers));
}

/* Latency ring access: returns (REV SUBMIT-MS COMMIT-MS VALUE COMMITTED)
   for the I-th most recent record (0 = most recent). */
DEFUN ("enca-evs-latency", Fenca_evs_latency, Senca_evs_latency, 1, 1, 0,
       doc: /* Return latency record I (0-based, newest first), or nil.  */)
  (Lisp_Object idx)
{
  CHECK_FIXNAT (idx);
  if (!evs_ring_fill)
    return Qnil;
  EMACS_INT i = XFIXNAT (idx);
  if ((enca_usize) i >= evs_ring_fill)
    return Qnil;
  enca_usize pos = (evs_ring_pos + EVS_RING - 1 - (enca_usize) i)
                   % EVS_RING;
  evs_lat_rec *rec = &evs_ring[pos];
  double ms = (double) (rec->commit_ns - rec->submit_ns) / 1e6;
  return listn (5, make_uint (rec->rev),
                make_float ((double) rec->submit_ns / 1e6),
                make_float (ms),
                make_uint (rec->value), rec->committed ? Qt : Qnil);
}

void
syms_of_enca_evs (void)
{
  DEFSYM (Qaccepted, "accepted");
  DEFSYM (Qreplaced, "replaced");
  DEFSYM (Qfolded, "folded");
  DEFSYM (Qexpired, "expired");

  defsubr (&Senca_evs_start);
  defsubr (&Senca_evs_stop);
  defsubr (&Senca_evs_on_change);
  defsubr (&Senca_evs_on_change_delta);
  defsubr (&Senca_evs_pump);
  defsubr (&Senca_evs_last_commit);
  defsubr (&Senca_evs_stats);
  defsubr (&Senca_evs_latency);
  defsubr (&Senca_evs_sync_analysis);
  defsubr (&Senca_evs_capture_stats);
  defsubr (&Senca_evs_delta_stats);
}

#endif /* HAVE_ENCA_EVS */
