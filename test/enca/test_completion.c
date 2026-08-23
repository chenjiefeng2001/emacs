/* EVS-4 correctness + workload-shape matrix for the completion
   substrate (bench/enca/evs4/COMPLETION.md sections 3-6).

   Gates are correctness oracles, absolute-budget checks and counting
   identities -- never micro-timing assertions. */

#include "test_util.h"

#include "../../src/enca/completion/completion.h"
#include "../../src/enca/scheduler/scheduler.h"
#include "../../src/enca/thread/thread.h"
#include "../../src/enca/time/time.h"
#include "../../src/enca/wake/wake.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------------- helpers ---------------- */

static void
ct_msleep (unsigned ms)
{
  static enca_wake_source *sleeper;
  if (!sleeper)
    {
      sleeper = malloc (sizeof *sleeper);
      if (enca_wake_init (sleeper) != ENCA_OK)
        return;
    }
  enca_wake_wait (sleeper, (enca_u64) ms * 1000000ull);
}

static void
fill_random (unsigned char *buf, size_t n)
{
  enca_u64 s = 0x9e3779b97f4a7c15ull ^ n;
  for (size_t i = 0; i < n; i++)
    {
      s ^= s << 13;
      s ^= s >> 7;
      s ^= s << 17;
      buf[i] = (unsigned char) (s >> 13);
    }
}

/* Build an incremental-storage snapshot of LEN bytes through
   doc_state (the NO-GO-but-frozen backend; completion must work over
   BOTH storages identically). */
static enca_document_snapshot *
build_doc (enca_snapshot_system *sys, enca_document *doc,
           const unsigned char *body, size_t len)
{
  enca_doc_state *ds = NULL;
  if (enca_doc_state_create (sys, doc, &ds) != ENCA_OK)
    return NULL;
  enca_document_snapshot *snap = NULL;
  if (enca_doc_state_edit (ds, 0, 0, body, len, &snap) != ENCA_OK)
    {
      enca_doc_state_destroy (ds);
      return NULL;
    }
  /* ds stays alive for the snapshot's lifetime; release root table
     ownership by destroying ds AFTER releasing snapshots is handled
     by caller ordering below (snapshot owns its own table ref). */
  enca_doc_state_destroy (ds);
  return snap;
}

/* ---------------- basic request lifecycle ---------------- */

static void
ct_basic (void)
{
  enca_id_registry reg;
  enca_snapshot_system sys;
  enca_document *doc = NULL;

  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);

  unsigned char body[512];
  fill_random (body, sizeof body);
  enca_document_snapshot *snap = build_doc (&sys, doc, body, sizeof body);
  CHECK (snap != NULL);

  enca_ct_request *r1 = NULL;
  CHECK_EQ_U64 (
      enca_ct_request_create (snap, doc->self_id, 1, ENCA_CT_MEMBER,
                              400, (const unsigned char *) "ba", 2,
                              144, 400, &r1),
      ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_requests_live (), 1);

  /* Clamping: cursor and inverted range stay inside the document. */
  enca_ct_request *r2 = NULL;
  CHECK_EQ_U64 (enca_ct_request_create (snap, doc->self_id, 1,
                                        ENCA_CT_PREFIX, 99999, NULL, 0,
                                        500, 100, &r2),
                ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_request_cursor (r2), 512);
  CHECK_EQ_U64 (enca_ct_request_snapshot (r2)->text.len, 512);

  /* Deterministic synthesis. */
  unsigned n1 = 0, n2 = 0;
  enca_u64 h1[ENCA_CT_MAX_CANDIDATES], h2[ENCA_CT_MAX_CANDIDATES];
  CHECK_EQ_U64 ((int) enca_ct_synth_server (r1, &n1, h1, NULL, NULL),
                (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_synth_server (r1, &n2, h2, NULL, NULL),
                (int) ENCA_OK);
  CHECK (n1 >= 5 && n1 <= ENCA_CT_MAX_CANDIDATES);
  CHECK_EQ_U64 (n1, n2);
  CHECK (memcmp (h1, h2, sizeof h1[0]) == 0);

  /* Different trigger => different candidates. */
  enca_u64 h3[ENCA_CT_MAX_CANDIDATES];
  unsigned n3 = 0;
  enca_ct_request *r3 = NULL;
  CHECK_EQ_U64 (enca_ct_request_create (snap, doc->self_id, 1,
                                        ENCA_CT_SYNTAX, 400,
                                        (const unsigned char *) "ba", 2,
                                        0, 400, &r3),
                ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_synth_server (r3, &n3, h3, NULL, NULL),
                (int) ENCA_OK);
  CHECK (!(n1 == n3 && memcmp (h1, h3, sizeof h1[0]) == 0));

  /* Invalid args propagate. */
  CHECK_EQ_U64 ((int) enca_ct_synth_server (NULL, &n1, h1, NULL, NULL),
                (int) ENCA_ERR_INVALID_ARGUMENT);

  enca_ct_request_destroy (r1);
  enca_ct_request_destroy (r2);
  enca_ct_request_destroy (r3);
  CHECK_EQ_U64 ((int) enca_ct_requests_live (), 0);
  enca_snapshot_release (snap);
  enca_document_destroy (doc);
  enca_snap_reclaim (&sys);
  enca_idr_destroy (&reg);
}

/* ---------------- extraction oracle over BOTH storages ---------------- */

static void
ct_extract_oracle (void)
{
  enca_id_registry reg;
  enca_snapshot_system sys;
  enca_document *doc_flat = NULL;
  enca_document *doc_incr = NULL;

  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc_flat), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc_incr), ENCA_OK);

  enum { LEN = 4096 };
  unsigned char *ref = malloc (LEN);
  fill_random (ref, LEN);

  /* Incremental path first: piece-backed storage, several edits,
     mirror applied to REF as we go. */
  enca_doc_state *ds = NULL;
  CHECK_EQ_U64 (enca_doc_state_create (&sys, doc_incr, &ds), ENCA_OK);
  enca_document_snapshot *incr = NULL;
  CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, 0, 0, ref, LEN, &incr),
                (int) ENCA_OK);
  enca_snapshot_release (incr);
  incr = NULL;
  for (int i = 0; i < 25; i++)
    {
      size_t pos = (size_t) (i * 163u) % LEN;
      unsigned char ch = (unsigned char) ('a' + i % 26);
      CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, pos, 1, &ch, 1,
                                            &incr),
                    (int) ENCA_OK);
      ref[pos] = ch;            /* mirror the replace */
      if (i < 24)
        enca_snapshot_release (incr);
    }

  /* Flat baseline: publish the SAME post-edit content through the
     full-capture path so both storages represent identical text. */
  enca_capture_input in = { ENCA_ENC_UTF8, ref, LEN };
  enca_document_snapshot *flat = NULL;
  CHECK_EQ_U64 ((int) enca_snapshot_publish (&sys, doc_flat, &in, 1,
                                          &flat),
                (int) ENCA_OK);

  /* Same regions extracted from both storages must match the mirror
     byte-for-byte. */
  static const size_t starts[] = { 0, 1, 63, 1023, 2048, 4095 };
  static const size_t spans[] = { 1, 32, 256, 1024 };
  for (size_t si = 0; si < sizeof starts / sizeof starts[0]; si++)
    for (size_t sp = 0; sp < sizeof spans / sizeof spans[0]; sp++)
      {
        size_t end = starts[si] + spans[sp];
        if (end > LEN)
          continue;
        unsigned char got_f[1024], got_i[1024];
        size_t lf = 0, li = 0;
        CHECK_EQ_U64 ((int) enca_ct_extract_context (
                          flat, starts[si], end, got_f, sizeof got_f,
                          &lf),
                      (int) ENCA_OK);
        CHECK_EQ_U64 ((int) enca_ct_extract_context (
                          incr, starts[si], end, got_i, sizeof got_i,
                          &li),
                      (int) ENCA_OK);
        CHECK_EQ_U64 (lf, spans[sp]);
        CHECK (lf == li && memcmp (got_f, got_i, lf) == 0);
        CHECK (memcmp (got_f, ref + starts[si], lf) == 0);
      }

  enca_snapshot_release (flat);
  enca_snapshot_release (incr);
  enca_doc_state_destroy (ds);
  enca_document_destroy (doc_flat);
  enca_document_destroy (doc_incr);
  enca_snap_reclaim (&sys);
  free (ref);
  enca_idr_destroy (&reg);
}

/* ---------------- O(region) extraction budget ---------------- */

static void
ct_o_region_budget (void)
{
  /* Contract section 8 gate: region extraction must stay far below
     any user-perceivable latency regardless of DOCUMENT size -- it
     may scale with pieces-touched and region bytes, never with the
     document. */
  long mb_env = 0;
  const char *env = getenv ("ENCA_CT_SWEEP");
  if (env && *env)
    mb_env = atol (env);

  const struct
  {
    size_t len;
    const char *label;
  } cells[]
    = { { 1u << 20, "1MB" }, { 8u << 20, "8MB" },
        { 10u << 20, "10MB" },
        { mb_env >= 2 ? 100u << 20 : 0, "100MB" } };

  enum { K = 2000, EDITS = 400 };
  for (int ci = 0; ci < 4; ci++)
    {
      if (cells[ci].len == 0)
        continue;
      size_t len = cells[ci].len;

      enca_id_registry reg;
      enca_snapshot_system sys;
      enca_document *doc = NULL;
      CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
      CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
      CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);

      unsigned char *body = malloc (len);
      CHECK (body != NULL);
      fill_random (body, len > (1u << 20) ? (1u << 20) : len);
      if (len > (1u << 20))
        memset (body + (1u << 20), 'x', len - (1u << 20));

      enca_doc_state *ds = NULL;
      CHECK_EQ_U64 (enca_doc_state_create (&sys, doc, &ds), ENCA_OK);
      enca_document_snapshot *snap = NULL;
      CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, 0, 0, body, len,
                                            &snap),
                    (int) ENCA_OK);
      enca_snapshot_release (snap);
      /* Fragment the table a little so the walk has real pieces to
         skip (metadata cost is part of the honest budget). */
      for (int i = 0; i < EDITS && len > EDITS; i++)
        {
          size_t pos = (size_t) ((i * 7919u) % len);
          unsigned char ch = (unsigned char) ('a' + i % 26);
          CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, pos, 1, &ch, 1,
                                                NULL),
                        (int) ENCA_OK);
          body[pos] = ch;
        }
      CHECK_EQ_U64 (
          (int) enca_doc_state_edit (ds, len / 2, 0, NULL, 0, &snap),
          (int) ENCA_OK);

      enca_ct_request *req = NULL;
      size_t start, end;
      enca_ct_context_window (ENCA_CT_MEMBER, len / 2, &start, &end);
      CHECK_EQ_U64 (enca_ct_request_create (snap, doc->self_id, 1,
                                            ENCA_CT_MEMBER, len / 2,
                                            NULL, 0, start, end,
                                            &req),
                    ENCA_OK);

      unsigned char buf[4096];
      size_t olen = 0;
      enca_u64 t0 = enca_monotonic_now_ns ();
      for (int i = 0; i < K; i++)
        CHECK_EQ_U64 ((int) enca_ct_extract_context (snap, start, end,
                                                  buf, sizeof buf,
                                                  &olen),
                      (int) ENCA_OK);
      double avg_us
        = (double) (enca_monotonic_now_ns () - t0) / K / 1000.0;
      printf ("    CTBUDGET|%s|avg=%.3fus|region=%llub|pieces=%zu\n",
              cells[ci].label, avg_us, (unsigned long long) (end - start),
              (size_t) enca_doc_state_piece_count (ds));
      fflush (stdout);
      CHECK (olen == end - start);
      /* Absolute budget: orders of magnitude below keypress latency.
         250us leaves room for pathological allocator behavior while
         still being invisible next to a 60Hz frame (16ms). */
      CHECK (avg_us < 250.0);

      enca_ct_request_destroy (req);
      enca_snapshot_release (snap);
      enca_doc_state_destroy (ds);
      enca_document_destroy (doc);
      enca_snap_reclaim (&sys);
      free (body);
      enca_idr_destroy (&reg);
    }
}

/* ---------------- EVS-4.2: completion storm ---------------- */

typedef struct
{
  enca_scheduler sched;
  enca_wake_source wake;
  enca_ct_request *req;         /* shared synthetic workload         */
} storm_h;

/* Leaf-safe observer per scheduler contract. */
static void
ct_notify_hook (void *ctx)
{
  enca_wake_notify ((enca_wake_source *) ctx);
}

static int
storm_exec (const enca_sched_task *t, void *ctx, enca_u64 *out)
{
  (void) t;
  storm_h *h = ctx;

  /* Optional artificial backend latency: models a real LSP round
     trip so supersession can actually bite.  ENCA_CT_STORM_DELAY_US.
     Busy-wait is fine here: executed tasks are few by construction. */
  static long delay_us = -1;
  if (delay_us < 0)
    {
      const char *d = getenv ("ENCA_CT_STORM_DELAY_US");
      delay_us = (d && *d) ? atol (d) : 0;
    }
  if (delay_us > 0)
    {
      enca_u64 until = enca_monotonic_now_ns ()
                       + (enca_u64) delay_us * 1000ull;
      while (enca_monotonic_now_ns () < until)
        enca_thread_yield ();
    }

  unsigned n = 0;
  enca_u64 hs[ENCA_CT_MAX_CANDIDATES];
  if (enca_ct_synth_server (h->req, &n, hs, NULL, NULL) != ENCA_OK)
    return -1;
  *out = hs[0];
  return 0;
}

static void
storm_commit (const enca_sched_result *r, void *ctx)
{
  (void) r;
  (void) ctx;
}

static bool
storm_pump_drain (storm_h *h)
{
  const enca_u64 deadline = enca_monotonic_now_ns () + 5000000000ull;
  for (;;)
    {
      enca_sched_poll (&h->sched, storm_commit, h);
      const enca_scheduler_stats *st = enca_sched_stats (&h->sched);
      if (atomic_load (&st->results_total)
          == atomic_load (&st->accepted))
        return true;            /* every accepted task has a result */
      if (enca_monotonic_now_ns () > deadline)
        return false;
      enca_wake_wait (&h->wake, 2 * 1000 * 1000);
    }
}

static void
ct_storm (void)
{
  /* Typing burst model: N keystrokes at INTERVAL ms each, each one a
     newer same-domain INTERACTIVE completion request.  Drop-before-
     compute must keep executed/submitted near 1 at storm rates. */
  static const unsigned intervals[] = { 0, 2, 10, 50 };
  enum { N = 12 };

  for (unsigned arm = 0; arm < sizeof intervals / sizeof intervals[0];
       arm++)
    {
      enca_id_registry reg;
      enca_snapshot_system sys;
      enca_document *doc = NULL;
      storm_h h;

      CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
      CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
      CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);

      unsigned char body[2048];
      fill_random (body, sizeof body);
      enca_document_snapshot *snap = build_doc (&sys, doc, body,
                                                sizeof body);
      CHECK (snap != NULL);
      CHECK_EQ_U64 (
          enca_ct_request_create (snap, doc->self_id, 1,
                                  ENCA_CT_MEMBER, 1024,
                                  (const unsigned char *) "ba", 2,
                                  768, 1024, &h.req),
          ENCA_OK);

      CHECK_EQ_U64 (enca_sched_init (&h.sched), ENCA_OK);
      CHECK_EQ_U64 (enca_wake_init (&h.wake), ENCA_OK);
      enca_sched_set_result_notify (&h.sched, ct_notify_hook,
                                    &h.wake);
      CHECK_EQ_U64 (enca_sched_start_workers (&h.sched, 1, storm_exec,
                                              &h),
                    ENCA_OK);

      long submitted = 0, queued = 0;
      for (int k = 0; k < N; k++)
        {
          if (k > 0 && intervals[arm] > 0)
            ct_msleep (intervals[arm]);
          enca_sched_task t;
          memset (&t, 0, sizeof t);
          t.document_id = 7;    /* same domain across the burst */
          t.cls = ENCA_TCLASS_INTERACTIVE;
          t.generation = 1;
          t.document_revision = (enca_u64) k + 1;
          t.urgency = ENCA_URGENCY_INTERACTIVE;
          t.deadline_ns = ENCA_DEADLINE_NONE;
          enca_admit_result r = enca_sched_submit (&h.sched, &t, NULL);
          submitted++;
          if (r == ENCA_ADMIT_ACCEPTED || r == ENCA_ADMIT_REPLACED)
            queued++;
        }

      CHECK (storm_pump_drain (&h));

      const enca_scheduler_stats *st = enca_sched_stats (&h.sched);
      long executed = (long) atomic_load (&st->executed);
      printf ("    CTSTORM|interval=%ums|submitted=%ld|queued=%ld|"
              "executed=%ld|folded=%llu\n",
              intervals[arm], submitted, queued, executed,
              (unsigned long long) atomic_load (&st->folded));
      fflush (stdout);

      /* Identities hold in every arm (#P3.2 section 9). */
      CHECK_EQ_U64 ((unsigned long long) atomic_load (&st->results_total),
                    (unsigned long long) atomic_load (&st->accepted));
      CHECK_EQ_U64 ((unsigned long long) atomic_load (&st->submitted),
                    (unsigned long long) (atomic_load (&st->accepted)
                                          + atomic_load (&st->folded)));

      if (intervals[arm] == 0)
        CHECK (executed <= 3);  /* storm: admission kills the rest */
      if (intervals[arm] == 50)
        CHECK (executed >= 8);  /* slow typing: most requests useful */

      enca_sched_shutdown (&h.sched);
      enca_sched_destroy (&h.sched);
      enca_wake_destroy (&h.wake);
      enca_ct_request_destroy (h.req);
      enca_snapshot_release (snap);
      enca_document_destroy (doc);
      enca_snap_reclaim (&sys);
      enca_idr_destroy (&reg);
    }
}

void
run_test_completion (void)
{
  enca_test_run_suite ("ct/basic", ct_basic);
  enca_test_run_suite ("ct/extract-oracle", ct_extract_oracle);
  enca_test_run_suite ("ct/o-region-budget", ct_o_region_budget);
  enca_test_run_suite ("ct/storm", ct_storm);
}
