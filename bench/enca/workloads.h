#ifndef ENCA_BENCH_WORKLOADS_H
#define ENCA_BENCH_WORKLOADS_H

/* Frozen workload corpus (P2.1.3).  Generators are deterministic:
   same seed + same edit index => same edit, on every machine. */

#include "editmodel.h"

typedef enum
{
  ENCA_WL_CODE_EDIT = 1,     /* W1: small local ins/del/replace      */
  ENCA_WL_TYPING = 2,        /* W2: one character per revision       */
  ENCA_WL_PASTE = 3,         /* W3: occasional large paste           */
  ENCA_WL_REFACTOR = 4,      /* W4: periodic ~1MB block replace      */
  ENCA_WL_BIGFILE_LOCAL = 5, /* W5: huge doc, edits in mid window    */
  ENCA_WL_SYNTHETIC = 6,     /* W6: parametric edit-size x locality   */
} enca_workload_kind;

enum
{
  ENCA_LOC_APPEND = 0,
  ENCA_LOC_MIDDLE = 1,
  ENCA_LOC_RANDOM = 2,
  ENCA_LOC_HOT = 3,
};

typedef struct enca_workload
{
  enca_workload_kind kind;
  enca_u64 seed;
  unsigned char *scratch;       /* heap: max insert payload 1MB      */
  enca_prng prng;
  long step;
  enca_u64 cursor;              /* W1 hot-region drift state         */
  enca_u64 synth_edit_size;     /* W6                                */
  int synth_locality;           /* W6: ENCA_LOC_*                    */
} enca_workload;

static inline void
enca_workload_destroy (enca_workload *w)
{
  free (w->scratch);
  w->scratch = NULL;
}

static inline void
enca_workload_init (enca_workload *w, enca_workload_kind kind,
                    enca_u64 seed)
{
  w->kind = kind;
  w->seed = seed;
  w->step = 0;
  w->scratch = malloc (1u << 20);
  w->cursor = 0;
  enca_prng_seed (&w->prng, seed * 6364136223846793005ull + 1442695040888963407ull);
}

static inline void
enca_workload_configure_synth (enca_workload *w, enca_u64 edit_size,
                               int locality)
{
  w->synth_edit_size = edit_size;
  w->synth_locality = locality;
}

static const char *
enca_workload_name (enca_workload_kind k)
{
  switch (k)
    {
    case ENCA_WL_CODE_EDIT: return "W1-code-edit";
    case ENCA_WL_TYPING: return "W2-typing";
    case ENCA_WL_PASTE: return "W3-paste";
    case ENCA_WL_REFACTOR: return "W4-refactor";
    case ENCA_WL_SYNTHETIC: return "W6-synthetic";
    case ENCA_WL_BIGFILE_LOCAL: return "W5-bigfile-local";
    }
  return "W?";
}

static inline void
fill_pattern (unsigned char *p, enca_usize n, enca_prng *prng, int variant)
{
  for (enca_usize i = 0; i < n; i++)
    p[i] = (unsigned char) (enca_prng_next (prng) ^ (enca_u64) variant
                            ^ (i * 31));
}

/* Produce the next edit.  cur_size lets generators clamp positions.
   Returns insert_len actually placed into e->insert_data. */
static inline enca_usize
enca_workload_next (enca_workload *w, enca_usize cur_size,
                    enca_edit_rec *e)
{
  enca_prng *p = &w->prng;
  w->step++;
  e->position = 0;
  e->delete_len = 0;
  e->insert_len = 0;
  e->insert_data = w->scratch;

  if (cur_size == 0)
    {
      fill_pattern (w->scratch, 16, p, w->step);
      e->insert_len = 16;
      return 16;
    }

  switch (w->kind)
    {
    case ENCA_WL_CODE_EDIT:
      {
        /* Local edits around a drifting cursor (hot region). */
        if (w->cursor > cur_size)
          w->cursor = cur_size / 2;
        long roll = (long) enca_prng_range (p, 3);
        enca_u64 span = cur_size > 8192 ? 4096 : cur_size;
        e->position = (w->cursor + enca_prng_range (p, span))
                      % (cur_size ? cur_size : 1);
        if (roll == 0)          /* small insert */
          {
            enca_usize n = 1 + enca_prng_range (p, 48);
            fill_pattern (w->scratch, n, p, w->step);
            e->insert_len = n;
          }
        else if (roll == 1)     /* small delete */
          {
            e->delete_len = 1 + enca_prng_range (p, 32);
            if (e->delete_len > cur_size - e->position)
              e->delete_len = cur_size - e->position;
          }
        else                    /* replace */
          {
            enca_usize n = 1 + enca_prng_range (p, 24);
            fill_pattern (w->scratch, n, p, w->step);
            e->insert_len = n;
            e->delete_len = 1 + enca_prng_range (p, 24);
            if (e->delete_len > cur_size - e->position)
              e->delete_len = cur_size - e->position;
          }
        w->cursor = e->position;
        break;
      }

    case ENCA_WL_TYPING:
      {
        /* One character at a time, mostly appending at the end.
           ENCA_W2_PURE=1 disables the 20% random-position inserts
           (debug bisecting). */
        static int pure;
        static int pure_init;
        if (!pure_init)
          {
            pure_init = 1;
            const char *pv = getenv ("ENCA_W2_PURE");
            pure = pv && pv[0] == '1';
          }
        w->scratch[0]
          = (unsigned char) ('a' + (int) enca_prng_range (p, 26));
        e->insert_len = 1;
        e->position = (pure || enca_prng_range (p, 100) < 80)
                        ? cur_size : enca_prng_range (p, cur_size);
        break;
      }

    case ENCA_WL_PASTE:
      {
        static const enca_usize sizes[]
          = { 1024, 10240, 102400, 1048576 };
        if (w->step % 20 == 0)
          {
            enca_usize n = sizes[w->step % 4];
            if (n > sizeof w->scratch)
              n = sizeof w->scratch;
            fill_pattern (w->scratch, n, p, w->step);
            e->insert_len = n;
            e->position = enca_prng_range (p, cur_size);
          }
        else
          {
            w->scratch[0] = (unsigned char) ('P');
            e->insert_len = 1;
            e->position = enca_prng_range (p, cur_size);
          }
        break;
      }

    case ENCA_WL_REFACTOR:
      {
        if (w->step % 10 == 0)
          {
            enca_usize n = 1u << 20; /* 1MB block replace */
            if (n > sizeof w->scratch)
              n = sizeof w->scratch;
            fill_pattern (w->scratch, n, p, w->step);
            e->insert_len = n;
            e->delete_len = n / 2;
            e->position = enca_prng_range (
              p, cur_size > n ? cur_size - n : 1);
          }
        else
          {
            enca_usize n = 1 + enca_prng_range (p, 64);
            fill_pattern (w->scratch, n, p, w->step);
            e->insert_len = n;
            e->position = enca_prng_range (p, cur_size);
          }
        break;
      }

    case ENCA_WL_SYNTHETIC:
      {
        /* Parametric edit-size x locality (P2.1.5 sweeps). */
        enca_u64 n = w->synth_edit_size ? w->synth_edit_size : 1;
        if (n > sizeof w->scratch)
          n = sizeof w->scratch;
        fill_pattern (w->scratch, n, p, w->step);
        e->insert_len = n;
        switch (w->synth_locality)
          {
          case ENCA_LOC_APPEND:
            e->position = cur_size;
            break;
          case ENCA_LOC_MIDDLE:
            e->position = cur_size / 2;
            break;
          case ENCA_LOC_RANDOM:
            e->position = enca_prng_range (p, cur_size);
            break;
          default: /* HOT */
            {
              enca_u64 base = cur_size / 2;
              e->position = base
                            - (base > 4096 ? 4096 : base)
                            + enca_prng_range (p, 8192);
              if (e->position > cur_size)
                e->position = cur_size;
            }
          }
        break;
      }

    case ENCA_WL_BIGFILE_LOCAL:
      {
        /* Confine to an 8KB window around the middle. */
        enca_u64 mid = cur_size / 2;
        e->position = mid - 4096 + enca_prng_range (p, 8192);
        if (e->position > cur_size)
          e->position = cur_size;
        enca_usize n = 1 + enca_prng_range (p, 256);
        fill_pattern (w->scratch, n, p, w->step);
        e->insert_len = n;
        e->delete_len = enca_prng_range (p, 128);
        if (e->delete_len > cur_size - e->position)
          e->delete_len = cur_size - e->position;
        break;
      }
    }

  return e->insert_len;
}

#endif
