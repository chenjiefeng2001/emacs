#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "arena.h"

#include "../base/assert.h"
#include <stdlib.h>
#include <string.h>

struct enca_arena_chunk
{
  enca_arena_chunk *next;
  enca_usize used;
  enca_usize cap;
  max_align_t data[];
};

static enca_usize
align_up (enca_usize n, enca_usize a)
{
  return (n + a - 1) & ~(a - 1);
}

void *
enca_arena_alloc_aligned (enca_arena *a, enca_usize n, enca_usize alignment)
{
  ENCA_ASSERT (a != NULL, "null arena");
  ENCA_ASSERT (alignment != 0 && (alignment & (alignment - 1)) == 0,
               "alignment must be power of two");

  if (!a || n == 0)
    return NULL;

  for (;;)
    {
      enca_arena_chunk *c = a->head;

      if (ENCA_LIKELY (c))
        {
          enca_uptr base = (enca_uptr) c->data + c->used;
          enca_uptr aligned = (base + alignment - 1) & ~(enca_uptr) (alignment - 1);
          enca_usize pad = (enca_usize) (aligned - base);

          if (pad <= c->cap - c->used && n <= c->cap - c->used - pad)
            {
              void *p = (void *) aligned;
              c->used += pad + n;
              a->total_allocated += n;
              return p;
            }
        }

      enca_usize want = align_up (sizeof (enca_arena_chunk), ENCA_ALIGNOF (max_align_t))
                        + n + alignment;
      enca_usize chunk_cap = want > a->default_chunk_size ? want : a->default_chunk_size;
      enca_arena_chunk *nc = malloc (chunk_cap);
      if (ENCA_UNLIKELY (nc == NULL))
        return NULL;
      nc->cap = chunk_cap - align_up (sizeof (enca_arena_chunk), ENCA_ALIGNOF (max_align_t));
      nc->used = 0;
      nc->next = a->head;
      a->head = nc;
      a->chunk_count++;
    }
}

void *
enca_arena_alloc (enca_arena *a, enca_usize n)
{
  return enca_arena_alloc_aligned (a, n, 16);
}

void
enca_arena_init (enca_arena *a, enca_usize chunk_size_hint)
{
  ENCA_ASSERT (a != NULL, "null arena");
  memset (a, 0, sizeof *a);
  a->default_chunk_size = chunk_size_hint < ENCA_ARENA_MIN_CHUNK
                            ? ENCA_ARENA_MIN_CHUNK
                            : chunk_size_hint;
}

void
enca_arena_reset (enca_arena *a)
{
  ENCA_ASSERT (a != NULL, "null arena");
  enca_arena_chunk *c = a->head;

  while (c && c->next)
    {
      enca_arena_chunk *dead = c;
      c = c->next;
      free (dead);
      a->chunk_count--;
    }

  a->head = c;

  if (a->head)
    {
      a->head->used = 0;
      a->chunk_count = 1;
    }
  else
    a->chunk_count = 0;

  a->total_allocated = 0;
}

void
enca_arena_destroy (enca_arena *a)
{
  ENCA_ASSERT (a != NULL, "null arena");
  enca_arena_chunk *c = a->head;

  while (c)
    {
      enca_arena_chunk *next = c->next;
      free (c);
      c = next;
    }
  memset (a, 0, sizeof *a);
}
