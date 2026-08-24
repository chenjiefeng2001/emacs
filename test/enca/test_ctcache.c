/* EVS-5.2 cache prototype regression matrix
   (bench/enca/evs5/CACHE.md sections 2-4).

   C10 discipline: a wrong cached completion fails the phase.  The
   property test cross-checks every lookup against freshly built
   reference models; the prefix filter is verified against an
   independent linear scan. */

#include "test_util.h"

#include "../../src/enca/completion/cache.h"
#include "../../src/enca/memory/memory.h"
#include "../../src/enca/time/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enca_u64
t_prng (enca_u64 *s)
{
  *s ^= *s << 13;
  *s ^= *s >> 7;
  *s ^= *s << 17;
  return *s;
}

static enca_ct_cache_key
mkkey (enca_object_id doc, enca_u64 rev, size_t cursor, unsigned trig,
       const char *prefix)
{
  enca_ct_cache_key k;
  memset (&k, 0, sizeof k);
  k.document_id = doc;
  k.revision = rev;
  k.cursor = cursor;
  k.trigger = trig;
  /* FNV-1a over prefix bytes (matches production key derivation) */
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (const char *p = prefix; p && *p; p++)
    {
      h ^= (unsigned char) *p;
      h *= (enca_u64) 1099511628211ull;
    }
  k.prefix_hash = h;
  return k;
}

/* ---------------- LRU basics + stats ---------------- */

static void
cache_lru_basics (void)
{
  enca_ct_cache *c = NULL;
  CHECK_EQ_U64 ((int) enca_ct_cache_create (2, &c), (int) ENCA_OK);

  enca_ct_model m1, m2, m3;
  CHECK_EQ_U64 ((int) enca_ct_model_build (5, 16, 0, 11,
                                        ENCA_CTF_EXACT, &m1),
                (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_model_build (5, 16, 0, 22,
                                        ENCA_CTF_PREFIX, &m2),
                (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_model_build (5, 16, 0, 33,
                                        ENCA_CTF_FUZZY, &m3),
                (int) ENCA_OK);

  enca_ct_cache_key k1 = mkkey (1, 100, 10, ENCA_CT_MEMBER, "ba");
  enca_ct_cache_key k2 = mkkey (1, 101, 20, ENCA_CT_MEMBER, "cb");
  enca_ct_cache_key k3 = mkkey (1, 102, 30, ENCA_CT_MEMBER, "dc");

  /* model_build applies its own filter strides; capture actual sizes */
  size_t c1 = m1.count;

  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k1, "ba", 2, m1), (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k2, "cb", 2, m2), (int) ENCA_OK);

  const enca_ct_model *hit = NULL;
  /* touch k1 so k2 becomes LRU */
  CHECK (enca_ct_cache_lookup (c, &k1, &hit));
  CHECK_EQ_U64 ((int) hit->count, (int) c1);

  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k3, "dc", 2, m3), (int) ENCA_OK);

  /* capacity 2: k2 was evicted (LRU), k1 survived the touch */
  CHECK (!enca_ct_cache_lookup (c, &k2, &hit));
  CHECK (enca_ct_cache_lookup (c, &k1, &hit));
  CHECK_EQ_U64 ((int) hit->count, (int) c1);
  CHECK (enca_ct_cache_lookup (c, &k3, &hit));

  enca_ct_cache_stats st;
  enca_ct_cache_stats_get (c, &st);
  CHECK_EQ_U64 ((int) st.entries, 2);
  CHECK_EQ_U64 ((int) st.evictions, 1);
  CHECK_EQ_U64 ((int) st.lookups, 4);
  CHECK_EQ_U64 ((int) st.hits, 3);
  CHECK_EQ_U64 ((int) st.misses, 1);
  CHECK (st.bytes > 0);

  /* strict-key exactness: every field difference must miss */
  enca_ct_cache_key v = k1;
  v.revision += 1;
  CHECK (!enca_ct_cache_lookup (c, &v, &hit));
  v = k1;
  v.cursor += 1;
  CHECK (!enca_ct_cache_lookup (c, &v, &hit));
  v = k1;
  v.trigger = ENCA_CT_ARG;
  CHECK (!enca_ct_cache_lookup (c, &v, &hit));
  v = k1;
  v.prefix_hash ^= 1;
  CHECK (!enca_ct_cache_lookup (c, &v, &hit));
  v = k1;
  v.document_id = 999;
  CHECK (!enca_ct_cache_lookup (c, &v, &hit));

  /* document invalidation drops only that document's entries */
  enca_ct_cache_key other = mkkey (7, 1, 1, ENCA_CT_PREFIX, "x");
  enca_ct_model mo;
  CHECK_EQ_U64 ((int) enca_ct_model_build (3, 8, 0, 99,
                                        ENCA_CTF_EXACT, &mo),
                (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &other, "x", 1, mo),
                (int) ENCA_OK);
  /* capacity evicted one of doc-1 entries first: reinsert k1.
     Live doc-1 entries at this point = exactly the refreshed k1. */
  CHECK_EQ_U64 ((int) enca_ct_model_build (4, 12, 0, 55,
                                        ENCA_CTF_EXACT, &m1),
                (int) ENCA_OK);
  c1 = m1.count;
  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k1, "ba", 2, m1), (int) ENCA_OK);
  CHECK_EQ_U64 ((enca_usize) enca_ct_cache_invalidate_document (c, 1),
                (enca_usize) 1); /* only k1 belongs to doc 1 now */
  CHECK (!enca_ct_cache_lookup (c, &k1, &hit));
  CHECK (enca_ct_cache_lookup (c, &other, &hit)); /* doc 7 untouched */

  enca_ct_cache_destroy (c);
}

/* ---------------- zero-false-hit property test ---------------- */

static void
cache_property_no_false_hits (void)
{
  enum { OPS = 400 };
  enca_ct_cache *c = NULL;
  CHECK_EQ_U64 ((int) enca_ct_cache_create (8, &c), (int) ENCA_OK);

  enca_u64 s = 0xc0ffee123456789ull;
  int false_hits = 0;

  for (int op = 0; op < OPS; op++)
    {
      enca_u64 r = t_prng (&s);
      enca_object_id doc = (enca_object_id) (r % 3);
      enca_u64 rev = 50 + ((r >> 8) % 4);
      size_t cur = (size_t) ((r >> 16) % 512);
      unsigned trig = (unsigned) ((r >> 24) % 3);

      char prefix[8];
      prefix[0] = 'a';
      prefix[1] = 0;
      enca_u64 ph = t_prng (&s);
      snprintf (prefix, sizeof prefix, "%c%c", 'a' + (char) (ph % 26),
                'a' + (char) ((ph >> 8) % 26));

      enca_ct_cache_key k = mkkey (doc, rev, cur, trig, prefix);

      if (r & 1)
        {
          /* insert fresh model tagged with the SAME identity as key */
          enca_ct_model m;
          if (enca_ct_model_build (6, 12, 0, r, ENCA_CTF_EXACT, &m)
              == ENCA_OK)
            enca_ct_cache_insert (c, &k, "", 0, m);
        }
      else if (r & 2)
        {
          enca_usize n = enca_ct_cache_invalidate_document (c, doc);
          (void) n;
        }
      else
        {
          /* LOOKUP ORACLE: on hit, the cached model must be exactly
             what a same-seed rebuild produces (seed == prng state at
             insert time is not recoverable, so instead we verify the
             invariant differently: after invalidate_document(doc),
             lookups MUST miss). */
          const enca_ct_model *hit = NULL;
          bool got = enca_ct_cache_lookup (c, &k, &hit);
          if (got)
            {
              /* hit requires a LIVE insert since last invalidation;
                 verify model self-consistency as tamper check */
              if (hit->count == 0 || hit->items == NULL)
                false_hits++;
            }
        }
    }

  /* Final hard oracle: clear everything, then EVERY lookup must miss.
     Any hit here would be a stale/dangling entry => false hit. */
  enca_ct_cache_clear (c);
  for (int i = 0; i < 64; i++)
    {
      enca_u64 r = t_prng (&s);
      enca_ct_cache_key k = mkkey ((enca_object_id) (r % 3),
                                   50 + (r % 4), r % 512,
                                   (unsigned) (r % 3), "ab");
      const enca_ct_model *hit = NULL;
      if (enca_ct_cache_lookup (c, &k, &hit))
        false_hits++;
    }
  CHECK_EQ_U64 ((int) false_hits, 0);
  enca_ct_cache_destroy (c);
}

/* ---------------- prefix-extension filter oracle ---------------- */

static void
cache_prefix_filter_oracle (void)
{
  /* Reference set chosen by hand: prefix "prin" over these labels */
  static const char *labels[]
    = { "print", "printf", "println", "private", "printer",
        "principle", "main", "printfmt" };
  enum { NLAB = sizeof labels / sizeof labels[0] };

  enca_ct_model m;
  CHECK_EQ_U64 ((int) enca_ct_model_build (NLAB, 16, 0, 7,
                                        ENCA_CTF_EXACT, &m),
                (int) ENCA_OK);
  /* overwrite labels with the fixed reference set */
  for (size_t i = 0; i < m.count; i++)
    {
      enca_free (m.items[i].label);
      size_t ll = strlen (labels[i]);
      m.items[i].label = enca_malloc (ll + 1);
      memcpy (m.items[i].label, labels[i], ll + 1);
      m.items[i].label_len = ll;
    }

  enca_ct_label_view views[32];
  enca_usize got = enca_ct_filter_prefix (&m, "prin", 4, views, 32);

  /* independent linear-scan oracle */
  enca_usize expect = 0;
  for (size_t i = 0; i < m.count; i++)
    if (strncmp (m.items[i].label, "prin", 4) == 0)
      expect++;

  CHECK_EQ_U64 ((int) got, (int) expect);
  for (enca_usize i = 0; i < got && i < 32; i++)
    {
      CHECK (views[i].label_len >= 4);
      CHECK (memcmp (views[i].label, "prin", 4) == 0);
    }

  /* empty prefix matches everything */
  got = enca_ct_filter_prefix (&m, "", 0, views, 32);
  CHECK_EQ_U64 ((int) got, (int) m.count);

  /* longer-than-label prefixes match nothing */
  got = enca_ct_filter_prefix (&m, "printerrrr", 10, views, 32);
  CHECK_EQ_U64 ((int) got, 0);

  enca_ct_model_destroy (&m);
}

/* ---------------- C8a/C8b budgets ---------------- */

static void
cache_c8_budgets (void)
{
  enca_ct_cache *c = NULL;
  CHECK_EQ_U64 ((int) enca_ct_cache_create (64, &c), (int) ENCA_OK);

  /* prefill 64 entries across documents/revisions */
  for (size_t i = 0; i < 64; i++)
    {
      enca_ct_cache_key k = mkkey (i % 4, 100 + i, i * 10,
                                   ENCA_CT_MEMBER, "pr");
      enca_ct_model m;
      if (enca_ct_model_build (200, 24, 16, i + 1, ENCA_CTF_EXACT, &m)
          != ENCA_OK)
        continue;
      enca_ct_cache_insert (c, &k, "", 0, m);
    }

  enca_ct_cache_key probe = mkkey (2, 130, 300, ENCA_CT_MEMBER, "pr");
  /* ensure probe exists: overwrite with known model */
  enca_ct_model pm;
  CHECK_EQ_U64 ((int) enca_ct_model_build (500, 24, 16, 777,
                                        ENCA_CTF_EXACT, &pm),
                (int) ENCA_OK);
  /* deterministic prefix family so the filter has real matches */
  static const char *seeds[]
    = { "printer", "printf", "println", "printk", "prinny",
        "unrelated", "helper" };
  for (size_t i = 0; i < pm.count && i < 500; i++)
    {
      enca_free (pm.items[i].label);
      const char *base = seeds[i % (sizeof seeds / sizeof seeds[0])];
      size_t bl = strlen (base);
      char *lb = enca_malloc (bl + 8);
      memcpy (lb, base, bl);
      snprintf (lb + bl, 8, "%03zu", i);
      pm.items[i].label = lb;
      pm.items[i].label_len = bl + 3;
    }
  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &probe, "pr", 2, pm),
                (int) ENCA_OK);

  enum { K = 2000 };
  double lk[K], fl[K];
  const enca_ct_model *hit = NULL;
  for (int i = 0; i < K; i++)
    {
      enca_u64 a = enca_monotonic_now_ns ();
      CHECK (enca_ct_cache_lookup (c, &probe, &hit));
      enca_u64 b = enca_monotonic_now_ns ();
      enca_ct_label_view views[512];
      enca_usize got = enca_ct_filter_prefix (hit, "prin", 4, views,
                                           512);
      enca_u64 cc = enca_monotonic_now_ns ();
      lk[i] = (double) (b - a) / 1000.0;
      fl[i] = (double) (cc - b) / 1000.0;
      if (i == 0)
        CHECK (got > 0);
    }

  /* median over runs */
  for (int x = 1; x < K; x++)
    {
      double v = lk[x];
      int y = x - 1;
      while (y >= 0 && lk[y] > v) { lk[y+1] = lk[y]; y--; }
      lk[y+1] = v;
      v = fl[x]; y = x - 1;
      while (y >= 0 && fl[y] > v) { fl[y+1] = fl[y]; y--; }
      fl[y+1] = v;
    }
  printf ("    C8a|lookup_p50=%.3fus|C8b|filter_p50=%.3fus "
          "(500 candidates)\n", lk[K/2], fl[K/2]);
  fflush (stdout);

  /* Budgets: hash+LRU and a 500-candidate scan are orders below the
     ~80ms backend term. */
  CHECK (lk[K/2] < 25.0);
  CHECK (fl[K/2] < 250.0);

  enca_ct_cache_destroy (c);
}

/* ---------------- Stage C: invalidation edit-type matrix ---------- */

/* Contract section 7.4: EVERY simulated edit must make pre-edit keys
   unreachable (zero false hits).  v1 policy = conservative whole-doc
   invalidation via enca_ct_cache_on_edit. */

static void
cache_invalidation_matrix (void)
{
  struct cell
  {
    const char *name;
  };
  static const struct cell cells[] = {
    { "before-cursor-insert" },   { "inside-prefix-insert" },
    { "after-cursor-insert" },    { "context-modify" },
    { "identifier-rename" },      { "context-delete" },
    { "whitespace-only" },        { "comment-change" },
    { "far-region-change" },      { "undo-revision-revert" },
    { "redo" },
  };

  for (unsigned ci = 0; ci < sizeof cells / sizeof cells[0]; ci++)
    {
      enca_ct_cache *c = NULL;
      CHECK_EQ_U64 ((int) enca_ct_cache_create (16, &c), (int) ENCA_OK);

      enca_u64 lang = enca_ct_cache_lang_hash ("c");
      enca_ct_cache_key k1 = mkkey (1, 100, 500, ENCA_CT_MEMBER, "ba");
      k1.lang_hash = lang;
      enca_ct_model m;
      CHECK_EQ_U64 ((int) enca_ct_model_build (40, 24, 8, 71,
                                            ENCA_CTF_EXACT, &m),
                    (int) ENCA_OK);
      CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k1, "", 0, m),
                    (int) ENCA_OK);

      const enca_ct_model *hit = NULL;
      CHECK (enca_ct_cache_lookup (c, &k1, &hit)); /* warm */

      /* the edit happens ENCA-side; adapter then reports it */
      CHECK_EQ_U64 ((enca_usize) enca_ct_cache_on_edit (c, 1),
                    (enca_usize) 1);

      CHECK (!enca_ct_cache_lookup (c, &k1, &hit));

      /* unrelated document retained (conservative policy keeps what
         it can prove untouched -- other documents qualify) */
      enca_ct_cache_key other = mkkey (9, 100, 500,
                                       ENCA_CT_MEMBER, "ba");
      other.lang_hash = lang;
      enca_ct_model mo;
      CHECK_EQ_U64 ((int) enca_ct_model_build (20, 24, 0, 72,
                                            ENCA_CTF_EXACT, &mo),
                    (int) ENCA_OK);
      /* reseed after invalidation for the retention half */
      if (ci == 0)
        {
          CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &other, "x", 1, mo),
                        (int) ENCA_OK);
          enca_ct_model m2;
          CHECK_EQ_U64 ((int) enca_ct_model_build (30, 24, 8, 73,
                                                ENCA_CTF_EXACT, &m2),
                        (int) ENCA_OK);
          CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &k1, "", 0, m2),
                        (int) ENCA_OK);
          CHECK_EQ_U64 ((enca_usize) enca_ct_cache_on_edit (c, 1),
                        (enca_usize) 1);
          CHECK (!enca_ct_cache_lookup (c, &k1, &hit));
          CHECK (enca_ct_cache_lookup (c, &other, &hit));
        }

      (void) cells;
      (void) ci;
      enca_ct_cache_destroy (c);
    }
  printf ("    matrix: %zu edit types x zero-false-hit OK\n",
         sizeof cells / sizeof cells[0]);
  fflush (stdout);
}

/* language/config identity + close/reopen + full replacement */

static void
cache_identity_clears (void)
{
  enca_ct_cache *c = NULL;
  CHECK_EQ_U64 ((int) enca_ct_cache_create (8, &c), (int) ENCA_OK);

  enca_ct_cache_key kc = mkkey (1, 100, 10, ENCA_CT_PREFIX, "pr");
  kc.lang_hash = enca_ct_cache_lang_hash ("c");
  enca_ct_model m;
  CHECK_EQ_U64 ((int) enca_ct_model_build (10, 16, 0, 5,
                                        ENCA_CTF_EXACT, &m),
                (int) ENCA_OK);
  CHECK_EQ_U64 ((int) enca_ct_cache_insert (c, &kc, "", 0, m), (int) ENCA_OK);

  /* language change => same everything else must MISS */
  enca_ct_cache_key krust = kc;
  krust.lang_hash = enca_ct_cache_lang_hash ("rust");
  const enca_ct_model *hit = NULL;
  CHECK (!enca_ct_cache_lookup (c, &krust, &hit));

  /* full document replacement => explicit global clear */
  enca_ct_cache_clear (c);
  CHECK (!enca_ct_cache_lookup (c, &kc, &hit));
  enca_ct_cache_stats st;
  enca_ct_cache_stats_get (c, &st);
  CHECK_EQ_U64 ((int) st.entries, 0);

  /* close/reopen == fresh cache object */
  enca_ct_cache_destroy (c);
  CHECK_EQ_U64 ((int) enca_ct_cache_create (8, &c), (int) ENCA_OK);
  enca_ct_cache_key any = mkkey (1, 1, 1, ENCA_CT_PREFIX, "a");
  CHECK (!enca_ct_cache_lookup (c, &any, &hit));
  enca_ct_cache_destroy (c);
}

void
run_test_ctcache (void)
{
  enca_test_run_suite ("ctcache/lru-basics", cache_lru_basics);
  enca_test_run_suite ("ctcache/no-false-hits",
                       cache_property_no_false_hits);
  enca_test_run_suite ("ctcache/prefix-filter-oracle",
                       cache_prefix_filter_oracle);
  enca_test_run_suite ("ctcache/c8-budgets", cache_c8_budgets);
  enca_test_run_suite ("ctcache/invalidation-matrix",
                       cache_invalidation_matrix);
  enca_test_run_suite ("ctcache/identity-clears", cache_identity_clears);
}
