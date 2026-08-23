/* EVS-4.4 native attribution: completion transformation matrix
   (bench/enca/evs4/UI_ATTRIBUTION.md section 3).

   Measures T6 (candidate model build), T7 (table-ready = model +
   filter pass), T8 (popup layout) across the frozen workload grid.
   These are the segments a future A2/A3 worker would own. */

#include "test_util.h"

#include "../../src/enca/completion/completion.h"
#include "../../src/enca/time/time.h"

#include <stdio.h>
#include <stdlib.h>

static double
pct_of (double *v, int n, int p)
{
  /* v is already sorted ascending */
  int idx = (int) ((double) p / 100.0 * (n - 1));
  return v[idx];
}

static void
ctpath_matrix (void)
{
  static const size_t counts[] = { 10, 100, 1000, 10000 };
  static const size_t lens[] = { 8, 32, 128, 512 };
  static const size_t annots[] = { 0, 16 };
  static const enca_ct_filter filters[]
    = { ENCA_CTF_EXACT, ENCA_CTF_PREFIX, ENCA_CTF_FUZZY };

  for (unsigned ci = 0; ci < sizeof counts / sizeof counts[0]; ci++)
    for (unsigned li = 0; li < sizeof lens / sizeof lens[0]; li += 1)
      {
        /* Keep the grid bounded: length sweep only on the two
           extreme counts, middle count carries the annotation and
           filter sweeps. */
        unsigned li_eff = (counts[ci] == 100 || counts[ci] == 10000)
                            ? li
                            : (li == 0 ? 1 : 99);
        if (li_eff >= sizeof lens / sizeof lens[0])
          continue;
        size_t ll = lens[li_eff];

        for (unsigned ai = 0; ai < sizeof annots / sizeof annots[0];
             ai++)
          {
            if (!(counts[ci] == 100) && ai > 0)
              continue;         /* annotation sweep only @count=100 */
            for (unsigned fi = 0;
                 fi < sizeof filters / sizeof filters[0]; fi++)
              {
                if (!(counts[ci] == 100 || counts[ci] == 10) && fi > 0)
                  continue;     /* filter sweep only on small counts */

                enum { K = 50 };
                double t6[K], t78[K], t8[K];
                enca_ct_model m;
                memset (&m, 0, sizeof m);
                int ok_runs = 0;

                for (int k = 0; k < K; k++)
                  {
                    enca_u64 a = enca_monotonic_now_ns ();
                    if (enca_ct_model_build (counts[ci], ll,
                                             annots[ai],
                                             (enca_u64) k + 1,
                                             filters[fi], &m)
                        != ENCA_OK)
                      break;
                    enca_u64 b = enca_monotonic_now_ns ();
                    enca_ct_popup_layout pl;
                    enca_ct_popup_layout_calc (&m, 10, m.count / 2, &pl);
                    enca_u64 c = enca_monotonic_now_ns ();

                    t6[ok_runs] = (double) (b - a) / 1000.0;
                    t78[ok_runs] = (double) (c - b) / 1000.0;
                    t8[ok_runs] = (double) (c - a) / 1000.0;
                    ok_runs++;
                    enca_ct_model_destroy (&m);
                  }
                if (ok_runs == 0)
                  continue;

                double s6[K], s78[K], s8[K];
                memcpy (s6, t6, sizeof (double) * ok_runs);
                memcpy (s78, t78, sizeof (double) * ok_runs);
                memcpy (s8, t8, sizeof (double) * ok_runs);
                for (int x = 1; x < ok_runs; x++)
                  {
                    double v;
                    v = s6[x];
                    int y = x - 1;
                    while (y >= 0 && s6[y] > v)
                      { s6[y + 1] = s6[y]; y--; }
                    s6[y + 1] = v;
                    v = s78[x]; y = x - 1;
                    while (y >= 0 && s78[y] > v)
                      { s78[y + 1] = s78[y]; y--; }
                    s78[y + 1] = v;
                    v = s8[x]; y = x - 1;
                    while (y >= 0 && s8[y] > v)
                      { s8[y + 1] = s8[y]; y--; }
                    s8[y + 1] = v;
                  }

                printf ("    CTM|count=%zu|len=%zu|annot=%zu|"
                        "filter=%d|kept=%d|"
                        "build_us=%.2f|popup_us=%.3f|total_us=%.2f\n",
                        counts[ci], ll, annots[ai], (int) filters[fi],
                        ok_runs, pct_of (s6, ok_runs, 50),
                        pct_of (s78, ok_runs, 50),
                        pct_of (s8, ok_runs, 50));
                fflush (stdout);

                /* Sanity: popup geometry consistent. */
                CHECK (ok_runs == K);
              }
          }
      }
}

void
run_test_ctpath (void)
{
  enca_test_run_suite ("ctpath/matrix", ctpath_matrix);
}
