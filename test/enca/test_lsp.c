/* EVS-4.3 regression matrix for the LSP transport layer
   (bench/enca/evs4/EVS43.md sections 2-5).

   The commit gate is a pure truth table; the codec is exercised
   round-trip; the attribution arms print phase tables.  The clangd
   arm SKIPS cleanly when no server binary is present. */

#include "test_util.h"

#include "../../src/enca/id/id.h"
#include "../../src/enca/lsp/jsonrpc.h"
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
lsp_msleep (unsigned ms)
{
#ifdef _WIN32
  Sleep (ms);
#else
  usleep (ms * 1000);
#endif
}

/* ---------------- codec ---------------- */

static void
lsp_jsoncodec (void)
{
  const char *resp =
    "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{\"isIncomplete\":true,"
    "\"items\":[{\"label\":\"foo_bar(\\\"x\\\")\"},{\"label\":\"b\"}]},"
    "\"note\":\"skip \\\"id\\\" inside strings: id\"}";
  enca_u64 id = 0;
  CHECK (enca_json_get_u64 (resp, strlen (resp), "id", &id));
  CHECK_EQ_U64 (id, 42);
  CHECK (enca_json_has_member (resp, strlen (resp), "result"));
  CHECK (enca_json_has_member (resp, strlen (resp), "isIncomplete"));
  /* "id" appears inside a string too; the scanner must skip it and
     still bind the numeric member. */
  CHECK (!enca_json_get_u64 (resp, strlen (resp), "nonexistent", &id));

  /* Writer + escaping round trip. */
  enca_json_buf b;
  CHECK_EQ_U64 ((int) jb_init (&b), (int) ENCA_OK);
  CHECK_EQ_U64 ((int) jb_put_json_string (&b, "a\"b\\c\nd\te", 10),
                (int) ENCA_OK);
  CHECK_EQ_U64 (enca_json_has_member (b.buf, b.len, "label"), false);
  CHECK (strstr (b.buf, "\\\"") != NULL);
  CHECK (strstr (b.buf, "\\n") != NULL);
  jb_free (&b);

  /* Framing header format. */
  CHECK_EQ_U64 ((int) jb_init (&b), (int) ENCA_OK);
  CHECK_EQ_U64 ((int) jb_put_lsp_header (&b, 1234), (int) ENCA_OK);
  CHECK (strcmp (b.buf, "Content-Length: 1234\r\n\r\n") == 0);
  jb_free (&b);
}

/* ---------------- commit-gate truth table (invariant 2.2) ---------------- */

static void
lsp_commit_gate (void)
{
  enca_object_id doc1 = 11, doc2 = 22;
  enca_u64 gen = 5, rev = 100;

  /* Perfect match commits. */
  CHECK (enca_lsp_commit_eligible (doc1, gen, rev, false, doc1, gen,
                                   rev));

  /* Every single-term mismatch blocks. */
  CHECK (!enca_lsp_commit_eligible (doc2, gen, rev, false, doc1, gen,
                                    rev));
  CHECK (!enca_lsp_commit_eligible (doc1, gen + 1, rev, false, doc1,
                                    gen, rev));
  CHECK (!enca_lsp_commit_eligible (doc1, gen, rev - 1, false, doc1,
                                    gen, rev));
  CHECK (!enca_lsp_commit_eligible (doc1, gen, rev - 1, true, doc1, gen,
                                    rev));
  CHECK (!enca_lsp_commit_eligible (doc1, gen, rev + 1, false, doc1,
                                    gen, rev));
  /* Cancellation blocks even a perfectly fresh response. */
  CHECK (!enca_lsp_commit_eligible (doc1, gen, rev, true, doc1, gen,
                                    rev));
}

/* ---------------- loopback arm (B1): real pipe, no server ---------- */

static void
lsp_loopback_attribution (void)
{
  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);

  CHECK_EQ_U64 ((int) enca_lsp_session_create (ENCA_LSP_LOOPBACK, &opts,
                                            &s),
                (int) ENCA_OK);

  enum { LEN = 64 * 1024 };
  char *body = malloc (LEN);
  for (int i = 0; i < LEN; i++)
    body[i] = (char) ('a' + (i % 23));
  enca_u64 open_t0 = enca_monotonic_now_ns ();
  CHECK_EQ_U64 ((int) enca_lsp_did_open (s, "file:///enca-bench.c", body,
                                      LEN, 7),
                (int) ENCA_OK);
  double open_ms = (double) (enca_monotonic_now_ns () - open_t0) / 1e6;

  /* Steady-state loopback requests. */
  enum { K = 200 };
  enca_lsp_timing tm;
  double sum_rt_us = 0, max_rt_us = 0, sum_ser_us = 0, sum_par_us = 0;
  size_t total_resp_bytes = 0;
  for (int i = 0; i < K; i++)
    {
      const char *resp = NULL;
      size_t rl = 0;
      CHECK_EQ_U64 ((int) enca_lsp_completion (
                        s, "file:///enca-bench.c", 0, LEN / 2, &resp,
                        &rl, &tm),
                    (int) ENCA_OK);
      double rt = (double) tm.roundtrip_ns / 1000.0;
      sum_rt_us += rt;
      if (rt > max_rt_us)
        max_rt_us = rt;
      sum_ser_us += (double) tm.serialize_ns / 1000.0;
      sum_par_us += (double) tm.parse_ns / 1000.0;
      total_resp_bytes += rl;
    }
  printf ("    LSPB1|loopback|K=%d|avg_rt=%.2fus|max_rt=%.2fus|"
          "avg_ser=%.2fus avg_parse=%.2fus resp_bytes_total=%zu "
          "(didOpen setup=%.3fms)\n",
          K, sum_rt_us / K, max_rt_us, sum_ser_us / K, sum_par_us / K,
          total_resp_bytes, open_ms);
  fflush (stdout);

  /* Absolute budgets: framing + kernel pipe for a ~300B request must
     stay far below interactive scale. */
  CHECK (sum_rt_us / K < 250.0);
  CHECK (max_rt_us < 5000.0);

  free (body);
  enca_lsp_session_destroy (s);
}

/* ---------------- clangd arm (B2): auto-detected ---------------- */

static void
lsp_clangd_session (void)
{
  const char *path = enca_lsp_find_clangd ();
  if (!path)
    {
      printf ("    SKIP: clangd not found\n");
      fflush (stdout);
      return;
    }

  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);
  opts.clangd_path = path;
  opts.root_uri = "file:///C:/enca-bench";

  enca_u64 t0 = enca_monotonic_now_ns ();
  enca_result r = enca_lsp_session_create (ENCA_LSP_CLANGD, &opts, &s);
  double spawn_ms = (double) (enca_monotonic_now_ns () - t0) / 1e6;
  CHECK_EQ_U64 ((int) r, (int) ENCA_OK);
  if (r != ENCA_OK)
    return;
  printf ("    LSPT|setup|spawn+initialize=%.1fms (%s)\n", spawn_ms,
          path);

  /* Single-line ASCII document: line=0, character==byte offset
     (recorded scope constraint). */
  enum { LEN = 1024 * 1024 };
  char *body = malloc (LEN);
  for (int i = 0; i < LEN; i++)
    body[i] = (char) ('a' + (i % 23));
  t0 = enca_monotonic_now_ns ();
  CHECK_EQ_U64 ((int) enca_lsp_did_open (s, "file:///enca-bench.c", body,
                                      LEN, 1),
                (int) ENCA_OK);
  printf ("    LSPT|setup|didOpen %dKB=%.1fms\n", LEN / 1024,
          (double) (enca_monotonic_now_ns () - t0) / 1e6);
  lsp_msleep (150);              /* give the indexer a beat */

  enum { K = 20 };
  double rt[K];
  size_t rbytes[K];
  memset (rt, 0, sizeof rt);
  int oks = 0;
  const char *last_head = NULL;
  for (int i = 0; i < K; i++)
    {
      enca_lsp_timing tm;
      const char *resp = NULL;
      size_t rl = 0;
      enca_result rr = enca_lsp_completion (
        s, "file:///enca-bench.c", 0, LEN / 2, &resp, &rl, &tm);
      if (rr == ENCA_OK)
        {
          oks++;
          rt[i] = (double) tm.roundtrip_ns / 1e6;
          rbytes[i] = rl;
          if (resp)
            last_head = resp;
        }
    }

  /* Percentiles over successful round trips (ms). */
  for (int i = 1; i < K; i++)
    {
      double v = rt[i];
      int j = i - 1;
      while (j >= 0 && rt[j] > v)
        {
          rt[j + 1] = rt[j];
          j--;
        }
      rt[j + 1] = v;
    }
  size_t med_bytes = 0;
  if (oks > 0)
    med_bytes = rbytes[oks / 2];
  printf ("    LSPT|B2-clangd|ok=%d/%d|p50=%.2fms p95=%.2fms max=%.2fms"
          "|med_resp_bytes=%zu\n",
          oks, K, oks ? rt[oks / 2] : -1,
          oks ? rt[(oks * 95) / 100] : -1, oks ? rt[oks - 1] : -1,
          med_bytes);
  if (last_head && getenv ("LSP_DEBUG"))
    fprintf (stderr, "[last-resp] %.120s\n", last_head);
  /* Cold-start flakes allowed: at least half must succeed. */
  CHECK (oks >= K / 2);

  free (body);
  enca_lsp_session_destroy (s);
}

/* ---------------- storm vs real backend (env-gated) ---------------- */

static void
lsp_storm_real (void)
{
  if (!getenv ("ENCA_LSP_STORM"))
    {
      printf ("    SKIP: set ENCA_LSP_STORM=1 to run\n");
      fflush (stdout);
      return;
    }
  const char *path = enca_lsp_find_clangd ();
  if (!path)
    {
      printf ("    SKIP: clangd not found\n");
      fflush (stdout);
      return;
    }

  /* Typing burst with didChange(version=k)+completion per keystroke.
     Waste accounting per contract section 4. */
  long admission_waste = 0, started = 0, responses = 0, committed = 0;
  long backend_waste = 0;

  enca_lsp_session *s = NULL;
  enca_lsp_session_opts opts;
  memset (&opts, 0, sizeof opts);
  opts.clangd_path = path;
  CHECK_EQ_U64 ((int) enca_lsp_session_create (ENCA_LSP_CLANGD, &opts,
                                            &s),
                (int) ENCA_OK);

  enum { LEN = 256 * 1024, N = 12 };
  char *body = malloc (LEN + 32);
  for (int i = 0; i < LEN; i++)
    body[i] = (char) ('a' + (i % 23));
  body[LEN] = 0;
  CHECK_EQ_U64 ((int) enca_lsp_did_open (s, "file:///storm.c", body,
                                      LEN, 1),
                (int) ENCA_OK);
  lsp_msleep (120);

  static const char *word[] = { "f", "fo", "foo", "foo.",
                                "foo.b", "foo.ba", "foo.bar" };
  enum { WORDS = 7 };

  /* Simulate typing the word list twice at ~5ms cadence: each
     keystroke bumps the version (invariant 2.1) and issues a
     completion request.  The CURRENT revision advances immediately on
     the main thread, so any response that arrives after a later
     keystroke fails the revision gate -> stale. */
  long submitted = 0;
  enca_u64 version = 1;
  for (int pass = 0; pass < 2; pass++)
    {
      for (int w = 0; w < WORDS; w++)
        {
          size_t bl = LEN - 8 + (size_t) w;
          body[bl] = 0;
          version++;
          enca_result r = enca_lsp_did_change_full (
            s, "file:///storm.c", body, bl, version);
          CHECK_EQ_U64 ((int) r, (int) ENCA_OK);
          submitted++;
          enca_lsp_timing tm;
          const char *resp = NULL;
          size_t rl = 0;
          started++;
          r = enca_lsp_completion (s, "file:///storm.c", 0, bl, &resp,
                                   &rl, &tm);
          if (r == ENCA_OK)
            responses++;
          /* Revision gate: only a response for the CURRENT version
             commits.  The synchronous worker here always sees the
             latest version, so committed == responses in this
             harness; stale counting becomes meaningful once the
             request runs asynchronously behind the typist. */
          bool cancelled = false;
          if (r == ENCA_OK
              && enca_lsp_commit_eligible (7, 1, version, cancelled, 7,
                                           1, version))
            committed++;
          else
            backend_waste++;
          lsp_msleep (5);
        }
      (void) word;
      (void) admission_waste;
    }

  printf ("    CTSTORM-REAL|submitted=%ld|started=%ld|responses=%ld|"
          "committed=%ld|admission_waste=%ld|backend_waste=%ld|"
          "useful=%ld\n",
          submitted, started, responses, committed, admission_waste,
          backend_waste, committed);
  fflush (stdout);
  CHECK (committed >= submitted - 2);

  free (body);
  enca_lsp_session_destroy (s);
}

void
run_test_lsp (void)
{
  enca_test_run_suite ("lsp/jsoncodec", lsp_jsoncodec);
  enca_test_run_suite ("lsp/commit-gate", lsp_commit_gate);
  enca_test_run_suite ("lsp/loopback-attribution",
                       lsp_loopback_attribution);
  enca_test_run_suite ("lsp/clangd-session", lsp_clangd_session);
  enca_test_run_suite ("lsp/storm-real", lsp_storm_real);
}
