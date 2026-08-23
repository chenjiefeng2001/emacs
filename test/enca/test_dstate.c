/* EVS-2.2 correctness matrix for the incremental document state.

Oracle: every revision's snapshot is verified against a flat
reference buffer via exact FNV hash equality (walk-based). */

#include "test_util.h"

#include "../../src/enca/snapshot/snapshot.h"
#include "../../src/enca/time/time.h"

#include <stdio.h>
#include <stdlib.h>

static enca_u64
ref_hash (const unsigned char *p, size_t n)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (size_t i = 0; i < n; i++)
    {
      h ^= p[i];
      h *= (enca_u64) 1099511628211ull;
    }
  return h;
}

static bool
hash_walk (const unsigned char *data, size_t len, void *ctx)
{
  enca_u64 *hp = ctx;
  for (size_t i = 0; i < len; i++)
    {
      *hp ^= data[i];
      *hp *= (enca_u64) 1099511628211ull;
    }
  return true;
}

static enca_u64
snap_fnv (const enca_document_snapshot *s)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  enca_snapshot_walk_text (s, hash_walk, &h);
  return h;
}

static void
ref_edit (unsigned char *buf, size_t *len, size_t start, size_t del_len,
          const unsigned char *ins, size_t ins_len)
{
  if (start > *len) start = *len;
  if (del_len > *len - start) del_len = *len - start;
  memmove (buf + start + ins_len, buf + start + del_len,
           *len - start - del_len);
  if (ins_len) memcpy (buf + start, ins, ins_len);
  *len = *len - del_len + ins_len;
}

/* Apply one edit to BOTH dstate and reference buffer, then verify the
   published snapshot against the reference via hash+length.  When OUT
   is non-NULL it receives one reference to the new snapshot on
   success (transferred, not released). */
static bool
apply_and_verify (enca_doc_state *ds, unsigned char *ref, size_t *rlen,
                  size_t start, size_t del_len,
                  const void *ins, size_t ins_len,
                  enca_document_snapshot **out)
{
  ref_edit (ref, rlen, start, del_len, ins, ins_len);

  enca_document_snapshot *snap = NULL;
  if (enca_doc_state_edit (ds, start, del_len, ins, ins_len, &snap)
      != ENCA_OK)
    return false;

  bool ok = snap->text.len == *rlen && snap_fnv (snap)
              == ref_hash (ref, *rlen);
  if (ok && out)
    *out = snap;
  else
    enca_snapshot_release (snap);
  return ok;
}

static void
test_dstate_basic (void)
{
  enca_id_registry reg;
  enca_snapshot_system sys;
  enca_document *doc = NULL;
  enca_doc_state *ds = NULL;

  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);
  CHECK_EQ_U64 (enca_doc_state_create (&sys, doc, &ds), ENCA_OK);
  CHECK_EQ_U64 (enca_doc_state_length (ds), 0);

  unsigned char ref[4096];
  size_t rlen = 0;

  /* insert at beginning / middle / end */
  const unsigned char p1[] = "ABC";
  CHECK (apply_and_verify (ds, ref, &rlen, 0, 0, p1, 3, NULL));
  const unsigned char p2[] = "DEF";
  CHECK (apply_and_verify (ds, ref, &rlen, 1, 0, p2, 3, NULL));
  /* delete at beginning */
  CHECK (apply_and_verify (ds, ref, &rlen, 0, 2, NULL, 0, NULL));
  /* replace in middle */
  const unsigned char p3[] = "XY";
  CHECK (apply_and_verify (ds, ref, &rlen, 1, 1, p3, 2, NULL));
  /* append at end */
  const unsigned char p4[] = "TAIL";
  CHECK (apply_and_verify (ds, ref, &rlen, rlen, 0, p4, 4, NULL));

  CHECK (enca_doc_state_piece_count (ds) > 0);
  CHECK (enca_doc_state_avg_piece_size (ds) > 0.0);

  /* delete spanning multiple pieces at once */
  CHECK (apply_and_verify (ds, ref, &rlen, 1, rlen - 2, NULL, 0, NULL));

  /* replace the whole remaining content in one edit */
  const unsigned char p5[] = "NEWDOC";
  CHECK (apply_and_verify (ds, ref, &rlen, 0, rlen, p5, 6, NULL));

  /* clamped offsets: past-the-end start behaves as append */
  const unsigned char p6[] = "!";
  CHECK (apply_and_verify (ds, ref, &rlen, 9999, 0, p6, 1, NULL));

  /* empty edit is a valid no-op revision */
  CHECK (apply_and_verify (ds, ref, &rlen, 3, 0, NULL, 0, NULL));

  enca_doc_state_destroy (ds);
  enca_document_destroy (doc);
  enca_snap_reclaim (&sys);
}

static void
test_dstate_retention (void)
{
  enca_id_registry reg;
  enca_snapshot_system sys;
  enca_document *doc = NULL;
  enca_doc_state *ds = NULL;

  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);
  CHECK_EQ_U64 (enca_doc_state_create (&sys, doc, &ds), ENCA_OK);

  unsigned char ref[4096];
  size_t rlen = 0;

  /* Hold a snapshot of an early revision. */
  const unsigned char base[] = "IMMUTABLE-BASE";
  enca_document_snapshot *held = NULL;
  CHECK (apply_and_verify (ds, ref, &rlen, 0, 0, base,
                           sizeof base - 1, &held));
  CHECK (held != NULL);
  const size_t held_len = rlen;

  /* Apply many further edits (fire-and-forget publications). */
  for (int i = 0; i < 100; i++)
    {
      char tag[24];
      int n = sprintf (tag, "-edit%03d", i);
      CHECK (apply_and_verify (ds, ref, &rlen, rlen, 0, tag,
                               (size_t) n, NULL));
    }

  /* The held snapshot must be byte-for-byte identical to its
     revision, even though the document moved far ahead (#18). */
  CHECK_EQ_U64 (snap_fnv (held), ref_hash (ref, held_len));

  enca_snapshot_release (held);
  enca_doc_state_destroy (ds);
  enca_document_destroy (doc);
  enca_snap_reclaim (&sys);
}

/* ---------------- EVS-2.4 pre-decision retention torture ---------------- */

/* Deterministic PRNG so the torture sweep is reproducible. */
static enca_u64
trand (void)
{
  static enca_u64 s = 0x243f6a8885a308d3ull;
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return s;
}

/* Apply one edit to BOTH dstate and reference buffer.  Verifies only
   the published length (cheap); content checks are explicit below.
   OUT_OPT optionally retains the new snapshot (transferred). */
static bool
torture_edit (enca_doc_state *ds, unsigned char *ref, size_t *rlen,
              size_t start, size_t del_len,
              const void *ins, size_t ins_len,
              enca_document_snapshot **out_opt)
{
  ref_edit (ref, rlen, start, del_len, ins, ins_len);

  enca_document_snapshot *snap = NULL;
  if (enca_doc_state_edit (ds, start, del_len, ins, ins_len, &snap)
      != ENCA_OK)
    return false;
  if (snap->text.len != *rlen)
    {
      enca_snapshot_release (snap);
      return false;
    }
  if (out_opt)
    *out_opt = snap;
  else
    enca_snapshot_release (snap);
  return true;
}

static bool
verify_full (const enca_document_snapshot *snap, const unsigned char *ref,
             size_t rlen)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  enca_snapshot_walk_text (snap, hash_walk, &h);
  return snap->text.len == rlen && h == ref_hash (ref, rlen);
}

/* Collect snapshot bytes [from, from+want) into OUT. */
typedef struct
{
  size_t skip;
  size_t want;
  size_t got;
  unsigned char *out;
} span_ctx;

static bool
span_walk (const unsigned char *data, size_t len, void *p)
{
  span_ctx *c = p;
  if (c->got >= c->want)
    return false;
  if (c->skip >= len)
    {
      c->skip -= len;
      return true;
    }
  size_t take = len - c->skip;
  if (take > c->want - c->got)
    take = c->want - c->got;
  memcpy (c->out + c->got, data + c->skip, take);
  c->got += take;
  c->skip = 0;
  return c->got < c->want;
}

/* Model: large document + many small edits; snapshots retained at
   three strides; retained revisions must stay byte-for-byte valid;
   lifecycle accounting must close exactly.  Scale via env
   ENCA_DSTATE_TORTURE_MB (default 8; formal evidence run: 100). */
static void
test_dstate_torture (void)
{
  long mb = 8;
  const char *env = getenv ("ENCA_DSTATE_TORTURE_MB");
  if (env && *env)
    {
      long v = atol (env);
      if (v >= 1 && v <= 1024)
        mb = v;
    }
  const size_t base_len = (size_t) mb << 20;

  enca_id_registry reg;
  enca_snapshot_system sys;
  enca_document *doc = NULL;
  enca_doc_state *ds = NULL;

  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);
  CHECK_EQ_U64 (enca_snap_init (&sys, &reg), ENCA_OK);
  CHECK_EQ_U64 (enca_document_create (&sys, &doc), ENCA_OK);
  CHECK_EQ_U64 (enca_doc_state_create (&sys, doc, &ds), ENCA_OK);

  unsigned char *ref = malloc (base_len);
  CHECK (ref != NULL);
  for (size_t i = 0; i < base_len; i++)
    ref[i] = (unsigned char) (trand () >> 11);

  const enca_u64 t0 = enca_monotonic_now_ns ();

  /* Revision 1: whole document as one piece. */
  size_t rlen = base_len;
  {
    enca_document_snapshot *s0 = NULL;
    CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, 0, 0, ref, base_len,
                                          &s0),
                  (int) 0);
    CHECK (verify_full (s0, ref, rlen));
    enca_snapshot_release (s0);
  }

  enum { CAP_MAX = 200, WIN = 256 };
  enca_document_snapshot *kept[CAP_MAX];
  size_t kept_rev[CAP_MAX];
  unsigned char kept_win[CAP_MAX][WIN];   /* ref window at hold time */
  size_t kept_off[CAP_MAX];
  enca_u64 kept_hash[CAP_MAX];            /* subset: full hash at hold */
  size_t kept_n = 0;

  const int edits = 1000;
  for (int i = 2; i <= edits; i++)
    {
      size_t pos = (size_t) (trand () % (enca_u64) rlen);
      unsigned char ch = (unsigned char) (trand () >> 13);
      bool keep = (i % 8 == 0 || i % 32 == 0 || i % 128 == 0);
      enca_document_snapshot *ks = NULL;
      CHECK (keep ? torture_edit (ds, ref, &rlen, pos, 1, &ch, 1, &ks)
                  : torture_edit (ds, ref, &rlen, pos, 1, &ch, 1, NULL));
      if (keep && ks)
        {
          CHECK (kept_n < CAP_MAX);
          /* Anchor the retained revision NOW: a byte window plus,
             for the stride-128 subset (+first/+last), the full FNV
             of exactly this revision's content. */
          size_t off = (size_t) (trand () % (enca_u64) rlen);
          span_ctx c = { off, WIN, 0, kept_win[kept_n] };
          enca_snapshot_walk_text (ks, span_walk, &c);
          CHECK_EQ_U64 (c.got, WIN);
          kept_off[kept_n] = off;
          kept_hash[kept_n] = 0;
          if (kept_n == 0 || i % 128 == 0)
            {
              enca_u64 h = (enca_u64) 1469598103934665603ull;
              enca_snapshot_walk_text (ks, hash_walk, &h);
              kept_hash[kept_n] = h;
            }
          kept[kept_n] = ks;
          kept_rev[kept_n] = (size_t) i;
          kept_n++;
        }

      /* Periodic full-content verification of the CURRENT revision. */
      if (i % 100 == 0)
        {
          enca_document_snapshot *cur = NULL;
          CHECK_EQ_U64 ((int) enca_doc_state_edit (ds, 0, 0, NULL, 0,
                                                &cur),
                        (int) 0);
          CHECK (verify_full (cur, ref, rlen));
          enca_snapshot_release (cur);
        }
    }

  /* Retained snapshots, hundreds of revisions later: every window
     byte-identical to its hold-time copy; subset hashes identical to
     their hold-time full-content hash; lengths unchanged. */
  for (size_t k = 0; k < kept_n; k++)
    {
      unsigned char cur[WIN];
      span_ctx c = { kept_off[k], WIN, 0, cur };
      enca_snapshot_walk_text (kept[k], span_walk, &c);
      CHECK_EQ_U64 (c.got, WIN);
      CHECK (memcmp (cur, kept_win[k], WIN) == 0);
      CHECK_EQ_U64 (kept[k]->text.len, rlen);
    }
  for (size_t k = 0; k < kept_n; k++)
    {
      if (kept_hash[k])
        {
          enca_u64 h = (enca_u64) 1469598103934665603ull;
          enca_snapshot_walk_text (kept[k], hash_walk, &h);
          CHECK_EQ_U64 (h, kept_hash[k]);
        }
    }

  const enca_u64 elapsed_ns = enca_monotonic_now_ns () - t0;

  printf ("  torture: %ldMB, %d edits, %zu retained, pieces=%zu "
          "avg=%.1f alloc_bytes=%llu len=%zu, %.1fms\n",
          mb, edits, kept_n,
          (size_t) enca_doc_state_piece_count (ds),
          enca_doc_state_avg_piece_size (ds),
          (unsigned long long) enca_doc_state_alloc_bytes (ds),
          (size_t) enca_doc_state_length (ds),
          (double) elapsed_ns / 1e6);
  fflush (stdout);

  /* Release everything, then require exact lifecycle closure:
     created == destroyed, live == 0, registry holds only the buffer. */
  for (size_t k = 0; k < kept_n; k++)
    enca_snapshot_release (kept[k]);
  enca_doc_state_destroy (ds);
  enca_document_destroy (doc);
  enca_snap_reclaim (&sys);

  enca_snap_stats st;
  enca_snap_stats_get (&sys, &st);
  CHECK_EQ_U64 (st.created, st.destroyed);
  CHECK_EQ_U64 (st.live, 0);
  CHECK_EQ_U64 (st.live_computed, 0);
  /* Buffer slot freed by document_destroy, snapshot slots by reclaim. */
  CHECK_EQ_U64 (enca_idr_live_count (&reg), 0);

  free (ref);
  enca_idr_destroy (&reg);
}

void
run_test_dstate (void)
{
  enca_test_run_suite ("dstate/basic", test_dstate_basic);
  enca_test_run_suite ("dstate/retention", test_dstate_retention);
  enca_test_run_suite ("dstate/torture", test_dstate_torture);
}
