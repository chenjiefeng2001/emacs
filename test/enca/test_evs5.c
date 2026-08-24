/* EVS-5.1..5.3 backend attribution experiments
   (bench/enca/evs5/EVS5.md sections 1-2).

   Attribution ONLY -- no optimization lives here.  All suites require
   a real clangd and SKIP cleanly when absent. */

#include "test_util.h"

#include "../../src/enca/lsp/lsp.h"
#include "../../src/enca/time/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <unistd.h>
#endif

static void
e5_msleep (unsigned ms)
{
#ifdef _WIN32
  Sleep (ms);
#else
  usleep (ms * 1000);
#endif
}

static const char *
e5_clangd_or_skip (void)
{
  const char *p = enca_lsp_find_clangd ();
  if (!p)
    {
      printf ("    SKIP: clangd not found\n");
      fflush (stdout);
    }
  return p;
}

/* filler + trigger document: single line ASCII, cursor at end. */
static char *
e5_body (size_t ctx_bytes, size_t *out_len)
{
  static const char trig[] = "foo.";
  size_t total = ctx_bytes + sizeof trig - 1;
  char *b = malloc (total);
  if (!b)
    return NULL;
  for (size_t i = 0; i < ctx_bytes; i++)
    b[i] = (char) ('a' + ((i * 7) % 26));
  memcpy (b + ctx_bytes, trig, sizeof trig - 1);
  *out_len = total;
  return b;
}

static double
e5_pctl (double *v, int n, int p)
{
  /* v sorted ascending */
  return v[(int) ((double) p / 100.0 * (n - 1))];
}

static void
e5_sort (double *v, int n)
{
  for (int x = 1; x < n; x++)
    {
      double val = v[x];
      int y = x - 1;
      while (y >= 0 && v[y] > val)
        {
          v[y + 1] = v[y];
          y--;
        }
      v[y + 1] = val;
    }
}

/* ---------------- EVS-5.1: context sweep ---------------- */

static void
evs51_context_sweep (void)
{
  const char *path = e5_clangd_or_skip ();
  if (!path)
    return;

  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);
  opts.clangd_path = path;
  CHECK_EQ_U64 ((int) enca_lsp_session_create (ENCA_LSP_CLANGD, &opts,
                                            &s),
                (int) ENCA_OK);
  enca_lsp_set_collect_timeout (s, 10000);

  static const size_t sizes[]
    = { 32, 256, 1024, 4096, 16384, 32768 };
  enca_u64 version = 0;

  for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++)
    {
      size_t blen = 0;
      char *body = e5_body (sizes[si], &blen);
      if (!body)
        continue;
      version++;
      /* First cell OPENS the document; later cells change it
         (invariant 2.1: version == ENCA revision sequence). */
      enca_result r = (si == 0)
        ? enca_lsp_did_open (s, "file:///evs51.c", body, blen, version)
        : enca_lsp_did_change_full (s, "file:///evs51.c", body, blen,
                                    version);
      if (r != ENCA_OK)
        {
          printf ("    CTXSWEEP|%zu|SETUP_FAIL r=%d\n", sizes[si],
                  (int) r);
          free (body);
          continue;
        }

      /* two warmups after the change; surface failures instead of
         silently skipping the cell */
      int wfail = 0;
      for (int w = 0; w < 2; w++)
        {
          const char *rp = NULL;
          size_t rl = 0;
          enca_result wr = enca_lsp_completion (s, "file:///evs51.c", 0,
                                             blen, &rp, &rl, NULL);
          if (wr != ENCA_OK)
            {
              if (++wfail == 1)
                printf ("    CTXSWEEP|%zu|WARMUP_FAIL r=%d\n",
                        sizes[si], (int) wr);
            }
          e5_msleep (100);
        }
      if (wfail >= 2)
        {
          free (body);
          continue;
        }

      enum { K = 12 };
      double rt[K];
      int oks = 0;
      for (int k = 0; k < K; k++)
        {
          enca_lsp_timing tm;
          const char *rp = NULL;
          size_t rl = 0;
          if (enca_lsp_completion (s, "file:///evs51.c", 0, blen, &rp,
                                   &rl, &tm)
              == ENCA_OK)
            {
              rt[oks++] = (double) tm.roundtrip_ns / 1e6;
            }
          e5_msleep (30);
        }
      if (oks)
        {
          e5_sort (rt, oks);
          printf ("    CTXSWEEP|%zu|n=%d|p50=%.2f|p95=%.2f|max=%.2f\n",
                  sizes[si], oks, e5_pctl (rt, oks, 50),
                  e5_pctl (rt, oks, 95), rt[oks - 1]);
        }
      fflush (stdout);
      free (body);
    }
  enca_lsp_session_destroy (s);
}

/* ---------------- EVS-5.2: stale backend work ---------------- */

enum
{
  STORM_N = 12
};

static void
storm_run (enca_lsp_session *s, const char *uri, size_t pos,
           unsigned cadence_ms, int do_cancel)
{
  /* Fire N requests at CADENCE_MS spacing (pipelined: no reads in
     between).  Optionally $/cancelRequest every id except the last.
     Then collect all arrivals and report the drain profile. */
  enca_u64 ids[STORM_N];
  enca_u64 send_ns[STORM_N];
  size_t blen = 0;

  char *body = e5_body (4096, &blen);
  free (body);                    /* body already open on session */

  for (int k = 0; k < STORM_N; k++)
    {
      if (k > 0 && cadence_ms)
        e5_msleep (cadence_ms);
      enca_u64 id = 0;
      if (enca_lsp_send_completion (s, uri, 0, pos, &id) != ENCA_OK)
        continue;
      ids[k] = id;
      send_ns[k] = enca_monotonic_now_ns ();
      if (do_cancel && k > 0)
        enca_lsp_cancel_id (s, ids[k - 1]); /* supersede previous */
    }
  const enca_u64 last_send = send_ns[STORM_N - 1];
  enca_lsp_set_collect_timeout (s, 800);

  int got = 0;
  double arrivals[STORM_N];
  long stale_after_final = 0;
  for (int k = 0; k < STORM_N; k++)
    {
      const char *rp = NULL;
      size_t rl = 0;
      enca_u64 arrive = 0;
      enca_result r = enca_lsp_collect_response (s, 0, &rp, &rl,
                                              &arrive);

      if (r == ENCA_ERR_TIMEOUT)
        break;              /* silence => no more pending responses */
      if (r == ENCA_OK || r == ENCA_ERR_CANCELLED)
        {
          arrivals[got++] = (double) (arrive - send_ns[0]) / 1e6;
          if (arrive > last_send)
            stale_after_final++;
        }
    }

  /* per-response inter-arrival gaps */
  double gaps[STORM_N];
  int ng = 0;
  for (int k = 1; k < got; k++)
    gaps[ng++] = arrivals[k] - arrivals[k - 1];

  e5_sort (arrivals, got);
  printf ("    STALE|cadence=%ums|cancel=%d|responses=%d/%d|"
          "drain_ms=%.2f|last_resp_ms=%.2f|stale_after_final=%ld\n",
          cadence_ms, do_cancel, got, STORM_N,
          got ? arrivals[got - 1] : -1.0,
          got ? arrivals[got - 1] : -1.0, stale_after_final);
  if (ng)
    {
      e5_sort (gaps, ng);
      printf ("    GAPS|cadence=%ums|cancel=%d|min=%.2f|med=%.2f|"
              "max=%.2f\n",
              cadence_ms, do_cancel, gaps[0],
              e5_pctl (gaps, ng, 50), gaps[ng - 1]);
    }
  fflush (stdout);
}

static void
evs52_stale_attribution (void)
{
  const char *path = e5_clangd_or_skip ();
  if (!path)
    return;

  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);
  opts.clangd_path = path;
  CHECK_EQ_U64 ((int) enca_lsp_session_create (ENCA_LSP_CLANGD, &opts,
                                            &s),
                (int) ENCA_OK);

  size_t blen = 0;
  char *body = e5_body (4096, &blen);
  CHECK_EQ_U64 ((int) enca_lsp_did_change_full (s, "file:///evs52.c",
                                             body, blen, 1),
                (int) ENCA_OK);
  e5_msleep (300);
  /* warmup so cold parse does not pollute storm numbers */
  for (int w = 0; w < 3; w++)
    {
      const char *rp = NULL;
      size_t rl = 0;
      enca_lsp_completion (s, "file:///evs52.c", 0, blen / 2, &rp, &rl,
                           NULL);
      e5_msleep (150);
    }
  free (body);

  storm_run (s, "file:///evs52.c", blen / 2, 0, 0);   /* pure burst */
  storm_run (s, "file:///evs52.c", blen / 2, 0, 1);   /* burst+cancel */
  storm_run (s, "file:///evs52.c", blen / 2, 10, 1);  /* fast typing */
  storm_run (s, "file:///evs52.c", blen / 2, 50, 1);  /* human typing */

  enca_lsp_session_destroy (s);
}

/* ---------------- EVS-5.3: cache locality ---------------- */

static double
cache_probe (enca_lsp_session *s, const char *uri, size_t pos, int reps)
{
  double lats[32];
  int n = reps < 32 ? reps : 32;
  for (int i = 0; i < n; i++)
    {
      enca_lsp_timing tm;
      const char *rp = NULL;
      size_t rl = 0;
      if (enca_lsp_completion (s, uri, 0, pos, &rp, &rl, &tm)
          != ENCA_OK)
        return -1.0;
      lats[i] = (double) tm.roundtrip_ns / 1e6;
    }
  e5_sort (lats, n);
  return e5_pctl (lats, n, 50);
}

static void
evs53_cache_locality (void)
{
  const char *path = e5_clangd_or_skip ();
  if (!path)
    return;

  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);
  opts.clangd_path = path;
  CHECK_EQ_U64 ((int) enca_lsp_session_create (ENCA_LSP_CLANGD, &opts,
                                            &s),
                (int) ENCA_OK);

  size_t blen = 0;
  char *body = e5_body (8192, &blen);
  CHECK_EQ_U64 ((int) enca_lsp_did_open (s, "file:///evs53.c", body,
                                      blen, 1),
                (int) ENCA_OK);
  enca_lsp_set_collect_timeout (s, 10000);
  e5_msleep (300);
  /* warmup */
  for (int w = 0; w < 3; w++)
    {
      const char *rp = NULL;
      size_t rl = 0;
      enca_lsp_completion (s, "file:///evs53.c", 0, blen / 2, &rp, &rl,
                           NULL);
      e5_msleep (120);
    }

  const size_t X = blen / 2;
  double t_first = cache_probe (s, "file:///evs53.c", X, 5);
  double t_repeat = cache_probe (s, "file:///evs53.c", X, 5);
  double t_plus1 = cache_probe (s, "file:///evs53.c", X + 1, 5);
  double t_minus1 = cache_probe (s, "file:///evs53.c", X - 1, 5);
  double t_far = cache_probe (s, "file:///evs53.c", 64, 5);

  /* new revision, identical position/content semantics: bump version
     by re-sending the SAME text (clangd sees version=k+1). */
  enca_result rr = enca_lsp_did_change_full (s, "file:///evs53.c", body,
                                          blen, 2);
  double t_newrev = -1.0;
  if (rr == ENCA_OK)
    {
      e5_msleep (80);
      t_newrev = cache_probe (s, "file:///evs53.c", X, 5);
    }

  printf ("    CACHE|first=%.2f|repeat=%.2f|plus1=%.2f|minus1=%.2f|"
          "far=%.2f|newrev_samepos=%.2f\n",
          t_first, t_repeat, t_plus1, t_minus1, t_far, t_newrev);
  fflush (stdout);

  free (body);
  enca_lsp_session_destroy (s);
}

void
run_test_evs5 (void)
{
  enca_test_run_suite ("evs51/context-sweep", evs51_context_sweep);
  enca_test_run_suite ("evs52/stale-attribution",
                       evs52_stale_attribution);
  enca_test_run_suite ("evs53/cache-locality", evs53_cache_locality);
}
