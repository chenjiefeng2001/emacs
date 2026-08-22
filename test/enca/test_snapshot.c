/* P2.0 snapshot foundation tests -- S-matrix per SNAPSHOT.md gate.

   S01-S06  content: empty / ascii / multibyte / mixed / binary / large
   S10-S14  lifetime: refcount balance, supersede, concurrent readers,
            document destroyed first, buffer mutates
   S20-S24  staleness: generation-only, revision-only, both, commit
   S30-S33  destruction paths: completion, cooperative cancel,
            engine drop, shutdown drain -- one destructor entry
   Canary:  created - destroyed == live at all times; live == 0 after
            every suite. */

#include "test_util.h"

#include "../../src/enca/snapshot/snapshot.h"
#include "../../src/enca/runtime/runtime.h"

#include <stdlib.h>

static enca_id_registry g_reg;
static enca_snapshot_system g_sys;
static bool g_ready;

static void
ensure_init (void)
{
  if (g_ready)
    return;
  CHECK_EQ_U64 (enca_idr_init (&g_reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&g_sys, &g_reg), ENCA_OK);
  g_ready = true;
}

static void
check_invariant (void)
{
  /* Deferred reclaims happen on the publishing (this) thread. */
  enca_snap_reclaim (&g_sys);
  enca_snap_stats st;
  enca_snap_stats_get (&g_sys, &st);
  CHECK_EQ_U64 (st.live, st.live_computed);
}

static void
spin_counter_ge (const enca_counter *c, enca_u64 target)
{
  for (long i = 0; i < 200000000L; i++)
    {
      if (enca_counter_get (c) >= target)
        return;
    }
}

static enca_u64
fnv1a_ref (const unsigned char *p, enca_usize n)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (enca_usize i = 0; i < n; i++)
    {
      h ^= p[i];
      h *= (enca_u64) 1099511628211ull;
    }
  return h;
}

static enca_document_snapshot *
publish_ok (enca_document *doc, const void *bytes, enca_usize len,
            enca_encoding_t enc, enca_u64 gen)
{
  enca_capture_input in = { enc, bytes, len };
  enca_document_snapshot *s = NULL;
  enca_result r = enca_snapshot_publish (&g_sys, doc, &in, gen, &s);
  CHECK_EQ_U64 (r, ENCA_OK);
  return s;
}

/* Publish and drop the caller reference immediately; only the
   document's publisher-slot reference survives. */
static void
publish_slot_only (enca_document *doc, const void *bytes, enca_usize len,
                   enca_encoding_t enc, enca_u64 gen)
{
  enca_document_snapshot *s = publish_ok (doc, bytes, len, enc, gen);
  enca_snapshot_release (s);
}

static void
spin_released_ge (enca_u64 target)
{
  for (long i = 0; i < 200000000L; i++)
    {
      enca_snap_stats st;
      enca_snap_stats_get (&g_sys, &st);
      if (st.released >= target)
        return;
    }
}

/* ---------------- S01-S06: content ---------------- */

static void
test_content (void)
{
  ensure_init ();
  enca_snap_stats st0;
  enca_snap_stats_get (&g_sys, &st0);

  static const char s03[] = "h\xc3\xa9llo"
                            " \xe4\xb8\x96\xe7\x95\x8c"
                            " \xf0\x9f\x8c\x8d";
  static const char s04[] = "mix:\tA\r\nB \xe4\xb8\xad tail";
  static const unsigned char s05[] = { 'b', 0, 1, 255, 0, 'y', 128 };

  struct case_
  {
    const char *name;
    const unsigned char *bytes;
    enca_usize len;
    enca_encoding_t enc;
  };
  static const struct case_ cases[] = {
    { "empty", (const unsigned char *) "", 0, ENCA_ENC_UTF8 },
    { "ascii", (const unsigned char *) "hello snapshot", 14,
      ENCA_ENC_UTF8 },
    { "multibyte", (const unsigned char *) s03, sizeof s03 - 1,
      ENCA_ENC_UTF8 },
    { "mixed", (const unsigned char *) s04, sizeof s04 - 1,
      ENCA_ENC_UTF8 },
    { "binary", s05, sizeof s05, ENCA_ENC_BINARY },
  };
  const int ncases = (int) (sizeof cases / sizeof cases[0]);

  enca_document *doc = NULL;
  CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
  CHECK (doc != NULL);
  CHECK (enca_id_valid (doc->self_id));
  CHECK_EQ_U64 (enca_document_revision (doc), 0);

  for (int i = 0; i < ncases; i++)
    {
      enca_document_snapshot *s
        = publish_ok (doc, cases[i].bytes, cases[i].len, cases[i].enc, 7);

      enca_utf8_view v = enca_snapshot_text (s);
      CHECK_EQ_U64 (v.len, cases[i].len);
      if (cases[i].len > 0)
        CHECK (memcmp (v.data, cases[i].bytes, cases[i].len) == 0);

      const enca_snapshot_epoch *e = enca_snapshot_epoch_of (s);
      CHECK_EQ_U64 (e->document_revision, (enca_u64) i + 1);
      CHECK_EQ_U64 (e->runtime_generation, 7);
      CHECK_EQ_U64 (enca_document_revision (doc), (enca_u64) i + 1);
      CHECK (enca_id_valid (enca_snapshot_identity (s)));
      CHECK (enca_id_type (enca_snapshot_identity (s))
             == ENCA_OBJ_SNAPSHOT);
      CHECK (enca_idr_is_alive (&g_reg, enca_snapshot_identity (s)));
      CHECK (s->source_encoding == cases[i].enc);

      enca_snapshot_release (s);
    }

  /* S06: large buffer (1 MiB pattern). */
  enum { LARGE = 1 << 20 };
  unsigned char *buf = malloc (LARGE);
  CHECK (buf != NULL);
  for (int i = 0; i < LARGE; i++)
    buf[i] = (unsigned char) (i * 131 + (i >> 9));
  enca_document_snapshot *sl
    = publish_ok (doc, buf, LARGE, ENCA_ENC_UTF8, 7);
  enca_utf8_view vl = enca_snapshot_text (sl);
  CHECK_EQ_U64 (vl.len, LARGE);
  CHECK (memcmp (vl.data, buf, LARGE) == 0);
  enca_snapshot_release (sl);
  free (buf);

  CHECK_EQ_U64 (enca_document_revision (doc), (enca_u64) ncases + 1);
  enca_document_destroy (doc);

  enca_snap_stats st1;
  enca_snap_stats_get (&g_sys, &st1);
  CHECK_EQ_U64 (st1.live, st0.live);
  check_invariant ();
}

/* ---------------- S10-S14: lifetime ---------------- */

typedef struct
{
  enca_document *doc;
  long iters;
  long reads;
} reader_arg;

static enca_result
reader_main (void *arg)
{
  reader_arg *ra = arg;
  for (long i = 0; i < ra->iters; i++)
    {
      enca_document_snapshot *s = enca_document_latest_acquire (ra->doc);
      if (s)
        {
          /* Read-only pass over immutable bytes. */
          enca_utf8_view v = enca_snapshot_text (s);
          volatile enca_u64 acc = 0;
          for (enca_usize j = 0; j < v.len; j++)
            acc += v.data[j];
          ra->reads++;
          enca_snapshot_release (s);
        }
    }
  return ENCA_OK;
}

static void
test_lifetime (void)
{
  ensure_init ();
  enca_snap_stats st0;
  enca_snap_stats_get (&g_sys, &st0);

  /* S10: publish -> release balances exactly. */
  {
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    enca_document_snapshot *s = publish_ok (doc, "x", 1, ENCA_ENC_UTF8, 1);
    enca_snap_stats st_a, st_b;
    enca_snap_stats_get (&g_sys, &st_a);
    enca_snapshot_release (s);
    enca_snap_stats_get (&g_sys, &st_b);
    CHECK_EQ_U64 (st_b.released, st_a.released + 1);
    enca_document_destroy (doc); /* drops publisher slot ref */
    check_invariant ();
  }

  /* S11: acquire keeps a superseded snapshot alive until last ref. */
  {
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    enca_document_snapshot *s1
      = publish_ok (doc, "rev-one", 7, ENCA_ENC_UTF8, 1);
    enca_object_id id1 = enca_snapshot_identity (s1);

    enca_document_snapshot *held = enca_snapshot_acquire (s1);
    CHECK (held == s1);
    enca_snapshot_release (s1); /* caller ref gone; slot keeps it */

    enca_document_snapshot *s2
      = publish_ok (doc, "rev-two!", 8, ENCA_ENC_UTF8, 1);
    /* Superseded but still alive and byte-exact (#18). */
    CHECK (enca_idr_is_alive (&g_reg, id1));
    enca_utf8_view v1 = enca_snapshot_text (held);
    CHECK_EQ_U64 (v1.len, 7);
    CHECK (memcmp (v1.data, "rev-one", 7) == 0);
    CHECK_EQ_U64 (enca_snapshot_epoch_of (held)->document_revision, 1);
    CHECK_EQ_U64 (enca_snapshot_epoch_of (s2)->document_revision, 2);

    enca_snapshot_release (held); /* now the last ref -> retired */
    enca_snap_reclaim (&g_sys);   /* publishing thread frees slot */
    CHECK (!enca_idr_is_alive (&g_reg, id1));

    enca_document_destroy (doc);
    enca_snapshot_release (s2);
    check_invariant ();
  }

  /* S12: concurrent readers while main publishes new revisions. */
  {
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    publish_slot_only (doc, "seed", 4, ENCA_ENC_UTF8, 1);

    enum { NR = 4, ITERS = 400 };
    reader_arg rarg[NR];
    enca_thread rt[NR];
    for (int i = 0; i < NR; i++)
      {
        rarg[i].doc = doc;
        rarg[i].iters = ITERS;
        rarg[i].reads = 0;
        CHECK_EQ_U64 (enca_thread_create (&rt[i], "snap-reader",
                                          reader_main, &rarg[i]),
                      ENCA_OK);
      }
    char buf[32];
    for (int rev = 2; rev <= 50; rev++)
      {
          int n = sprintf (buf, "revision-%d-payload", rev);
          publish_slot_only (doc, buf, (enca_usize) n, ENCA_ENC_UTF8, 1);
      }
    long total = 0;
    for (int i = 0; i < NR; i++)
      {
        CHECK_EQ_U64 (enca_thread_join (&rt[i]), ENCA_OK);
        total += rarg[i].reads;
      }
    CHECK (total > 0);
    CHECK_EQ_U64 (enca_document_revision (doc), 50);

    enca_document_destroy (doc);
    enca_snap_stats st_end;
    enca_snap_stats_get (&g_sys, &st_end);
    CHECK_EQ_U64 (st_end.live, st0.live);
    check_invariant ();
  }

  /* S13: document destroyed while its snapshot stays valid. */
  {
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    enca_document_snapshot *s
      = publish_ok (doc, "outlives-doc", 12, ENCA_ENC_UTF8, 3);
    enca_object_id sid = enca_snapshot_identity (s);

    enca_document_destroy (doc);
    doc = NULL;

    /* Snapshot identity survives until reclaimed, even though the
       document is gone (#18). */
    CHECK (enca_idr_is_alive (&g_reg, sid));
    enca_utf8_view v = enca_snapshot_text (s);
    CHECK_EQ_U64 (v.len, 12);
    CHECK (memcmp (v.data, "outlives-doc", 12) == 0);
    CHECK_EQ_U64 (enca_snapshot_epoch_of (s)->document_revision, 1);
    enca_snapshot_release (s);
    enca_snap_reclaim (&g_sys);
    CHECK (!enca_idr_is_alive (&g_reg, sid));
    check_invariant ();
  }

  /* S14: source mutates; old snapshot byte-for-byte identical. */
  {
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    publish_slot_only (doc, "generation-A-content", 20, ENCA_ENC_UTF8, 1);
    enca_document_snapshot *old = enca_document_latest_acquire (doc);

    unsigned char golden[20];
    memcpy (golden, enca_snapshot_text (old).data, 20);

    publish_slot_only (doc, "generation-B-CONTENT", 20, ENCA_ENC_UTF8, 1);

    enca_utf8_view vo = enca_snapshot_text (old);
    CHECK_EQ_U64 (vo.len, 20);
    CHECK (memcmp (vo.data, golden, 20) == 0);
    CHECK_EQ_U64 (enca_snapshot_epoch_of (old)->document_revision, 1);

    enca_document_snapshot *cur = enca_document_latest_acquire (doc);
    CHECK (memcmp (enca_snapshot_text (cur).data,
                   "generation-B-CONTENT", 20)
           == 0);
    CHECK_EQ_U64 (enca_snapshot_epoch_of (cur)->document_revision, 2);

    enca_snapshot_release (old);
    enca_snapshot_release (cur);
    enca_document_destroy (doc);
    check_invariant ();
  }

  enca_snap_stats st_final;
  enca_snap_stats_get (&g_sys, &st_final);
  CHECK_EQ_U64 (st_final.live, st0.live);
}

/* ---------------- S20-S24: two-level staleness ---------------- */

typedef struct
{
  enca_u64 gen;
  enca_u64 stream_rev;
  enca_u64 value;
} result_log_entry;

#define LOG_MAX 64

typedef struct
{
  result_log_entry entries[LOG_MAX];
  enca_usize n;
  const enca_document *doc;
  enca_u64 poll_gen;
} commit_ctx;

static void
commit_log_cb (const enca_task_result *tr, void *user)
{
  commit_ctx *ctx = user;
  if (ctx->n < LOG_MAX)
    {
      ctx->entries[ctx->n].gen = tr->revision;
      ctx->entries[ctx->n].stream_rev = tr->stream_revision;
      ctx->entries[ctx->n].value = tr->value;
      ctx->n++;
    }
}

static void
reset_rt (enca_runtime *rt)
{
  CHECK_EQ_U64 (enca_runtime_init (rt, 1, 16), ENCA_OK);
}

static void
stop_rt (enca_runtime *rt)
{
  CHECK_EQ_U64 (enca_runtime_shutdown (rt,
                                       enca_deadline_from_now_ms (2000)),
                ENCA_OK);
  enca_runtime_destroy (rt);
}

static void
test_stale (void)
{
  ensure_init ();
  enca_runtime rt;
  reset_rt (&rt);
  enca_document *doc = NULL;
  CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
  commit_ctx ctx;
  memset (&ctx, 0, sizeof ctx);
  ctx.doc = doc;

  /* S20: generation mismatch only -> engine drops before callback. */
  {
    publish_slot_only (doc, "gen-stale", 9, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt));
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    spin_counter_ge (&rt.tasks_completed_by_worker, 1);
    enca_runtime_advance_generation (&rt);
    enca_u64 dropped0 = enca_counter_get (&rt.results_dropped_stale);
    enca_u64 committed0 = enca_counter_get (&rt.results_committed);
    ctx.n = 0;
    ctx.poll_gen = enca_runtime_current_generation (&rt);
    /* Processed count includes engine-dropped results; the callback
       must stay silent for them. */
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, commit_log_cb, &ctx),
                  1);
    CHECK_EQ_U64 (enca_counter_get (&rt.results_dropped_stale),
                  dropped0 + 1);
    CHECK_EQ_U64 (enca_counter_get (&rt.results_committed), committed0);
    CHECK_EQ_U64 (ctx.n, 0);
  }

  /* S21/S22: revision mismatch with same generation -> callback sees
     it and classifies stale via epoch_current. */
  {
    enca_u64 committed_before = enca_counter_get (&rt.results_committed);
    publish_slot_only (doc, "rev-one-data", 12, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt));
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    spin_counter_ge (&rt.tasks_completed_by_worker, 2);
    publish_slot_only (doc, "rev-TWO-data", 12, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt)); /* same gen */

    ctx.n = 0;
    ctx.poll_gen = enca_runtime_current_generation (&rt);
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, commit_log_cb, &ctx),
                  1);
    CHECK_EQ_U64 (ctx.n, 1);
    enca_snapshot_epoch e = { ctx.entries[0].gen,
                              ctx.entries[0].stream_rev };
    CHECK (!enca_snapshot_epoch_current (doc, ctx.poll_gen, &e));
    /* "gen-stale" consumed revision 1; "rev-one-data" is revision 2. */
    CHECK_EQ_U64 (e.document_revision, 2);
    CHECK_EQ_U64 (ctx.entries[0].value,
                  fnv1a_ref ((const unsigned char *) "rev-one-data", 12));
    /* Sanity: the helper really compares BOTH levels -- right
       revision paired with a wrong generation must fail. */
    enca_snapshot_epoch wrong_gen = { ctx.entries[0].gen + 1,
                                      e.document_revision };
    CHECK (!enca_snapshot_epoch_current (doc, ctx.poll_gen, &wrong_gen));
    CHECK_EQ_U64 (enca_counter_get (&rt.results_committed),
                  committed_before + 1); /* engine metric semantics */
    CHECK (ctx.entries[0].value == fnv1a_ref ((const unsigned char *)
                                              "rev-one-data", 12));
  }

  /* S23: generation advanced while revision still latest ->
     engine drop, callback silent. */
  {
    publish_slot_only (doc, "still-latest", 12, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt));
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    spin_counter_ge (&rt.tasks_completed_by_worker, 3);
    enca_runtime_advance_generation (&rt);
    enca_u64 dropped0 = enca_counter_get (&rt.results_dropped_stale);
    ctx.n = 0;
    ctx.poll_gen = enca_runtime_current_generation (&rt);
    /* Processed includes the engine-dropped result (see S20). */
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, commit_log_cb, &ctx),
                  1);
    CHECK_EQ_U64 (enca_counter_get (&rt.results_dropped_stale),
                  dropped0 + 1);
    CHECK_EQ_U64 (ctx.n, 0);
  }

  /* S24: both levels match -> commit with exact payload hash. */
  {
    static const char content[] = "fresh-and-current";
    publish_slot_only (doc, content, sizeof content - 1, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt));
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    spin_counter_ge (&rt.tasks_completed_by_worker, 4);
    ctx.n = 0;
    ctx.poll_gen = enca_runtime_current_generation (&rt);
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, commit_log_cb, &ctx),
                  1);
    CHECK_EQ_U64 (ctx.n, 1);
    enca_snapshot_epoch e = { ctx.entries[0].gen,
                              ctx.entries[0].stream_rev };
    CHECK (enca_snapshot_epoch_current (doc, ctx.poll_gen, &e));
    CHECK_EQ_U64 (ctx.entries[0].value, fnv1a_ref ((const unsigned char *)
                                                   content,
                                                   sizeof content - 1));
  }

  enca_document_destroy (doc);
  stop_rt (&rt);
  check_invariant ();
}

/* ---------------- S30-S33: destruction paths ---------------- */

static void
test_destruction (void)
{
  ensure_init ();

  /* S30: normal completion routes through the single destructor.
     Proven by released-counter delta after polling a task. */
  {
    enca_runtime rt;
    reset_rt (&rt);
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    publish_slot_only (doc, "normal-path", 11, ENCA_ENC_UTF8,
                enca_runtime_current_generation (&rt));
    enca_snap_stats sa;
    enca_snap_stats_get (&g_sys, &sa);
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    spin_counter_ge (&rt.tasks_completed_by_worker, 1);
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, NULL, NULL), 1);
    spin_released_ge (sa.released + 1); /* destructor hook is async */
    enca_snap_stats sb;
    enca_snap_stats_get (&g_sys, &sb);
    CHECK_EQ_U64 (sb.released, sa.released + 1); /* hook ran */
    enca_document_destroy (doc);
    stop_rt (&rt);
  }

  /* S31+S32: cooperative cancellation (large input mid-hash) and
     queued-task cancellation both release through the hook.  Timing
     decides which path fires; correctness requires only that the
     task never commits and its reference is released exactly once. */
  {
    enca_runtime rt;
    reset_rt (&rt);
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    enum { BIG = 8 << 20 };
    unsigned char *big = malloc (BIG);
    CHECK (big != NULL);
    for (int i = 0; i < BIG; i++)
      big[i] = (unsigned char) (i ^ (i >> 7));
    publish_slot_only (doc, big, BIG, ENCA_ENC_BINARY,
                enca_runtime_current_generation (&rt));
    free (big);

    enca_u64 cancelled0 = enca_counter_get (&rt.tasks_cancelled_cooperative);
    enca_snap_stats sa;
    enca_snap_stats_get (&g_sys, &sa);
    CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
    enca_runtime_advance_generation (&rt); /* cancel as early as possible */
    spin_counter_ge (&rt.tasks_cancelled_cooperative, cancelled0 + 1);
    spin_released_ge (sa.released + 1); /* destructor hook is async */

    enca_snap_stats sb;
    enca_snap_stats_get (&g_sys, &sb);
    CHECK_EQ_U64 (sb.released, sa.released + 1); /* hook ran on cancel */
    CHECK_EQ_U64 (sb.destroyed, sa.destroyed);   /* snapshot itself kept:
                                                    caller/publisher refs
                                                    remain */
    CHECK_EQ_U64 (enca_runtime_poll_results (&rt, 16, NULL, NULL), 0);
    enca_document_destroy (doc);
    stop_rt (&rt);
  }

  /* S33: shutdown drain discards pending tasks through the same
     destructor; no snapshot leaks. */
  {
    enca_runtime rt;
    reset_rt (&rt);
    enca_document *doc = NULL;
    CHECK_EQ_U64 (enca_document_create (&g_sys, &doc), ENCA_OK);
    for (int i = 0; i < 4; i++)
      {
        char tag[24];
        int n = sprintf (tag, "drain-%02d", i);
        publish_slot_only (doc, tag, (enca_usize) n, ENCA_ENC_UTF8,
                    enca_runtime_current_generation (&rt));
        CHECK_EQ_U64 (enca_snap_submit_latest (doc, &rt), ENCA_OK);
      }
    CHECK_EQ_U64 (enca_runtime_shutdown (
                    &rt, enca_deadline_from_now_ms (5000)),
                  ENCA_OK);
    enca_runtime_destroy (&rt);
    /* Every submitted task's transferred reference must be gone;
       only the final publisher-slot ref of each revision remains,
       and document_destroy clears those. */
    enca_document_destroy (doc);
    /* Invariant holds once reclamation has run (L7). */
    check_invariant ();
  }

  /* Global canary: nothing leaked across all suites. */
  enca_snap_reclaim (&g_sys);
  {
    enca_snap_stats st;
    enca_snap_stats_get (&g_sys, &st);
    CHECK_EQ_U64 (st.live, 0);
    CHECK_EQ_U64 (st.destroyed, st.created);
  }
}

void
run_test_snapshot (void)
{
  enca_test_run_suite ("snapshot/content", test_content);
  enca_test_run_suite ("snapshot/lifetime", test_lifetime);
  enca_test_run_suite ("snapshot/stale", test_stale);
  enca_test_run_suite ("snapshot/destruction", test_destruction);
}
