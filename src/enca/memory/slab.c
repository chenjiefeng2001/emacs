#include "slab.h"

#include "../base/assert.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
# include <malloc.h>
#endif

#define ENCA_SLAB_MAX_ALIGN ((enca_usize) 4096)

static void *
chunk_alloc_aligned (enca_usize size, enca_usize align)
{
#if defined(_WIN32)
  return _aligned_malloc (size, align);
#else
  void *p = NULL;
  enca_usize a = align < sizeof (void *) ? sizeof (void *) : align;
  if (posix_memalign (&p, a, size) != 0)
    p = NULL;
  return p;
#endif
}

static void
chunk_free_aligned (void *p)
{
#if defined(_WIN32)
  _aligned_free (p);
#else
  free (p);
#endif
}

struct enca_slab_chunk
{
  enca_slab_chunk *next;
  void *raw;
  unsigned char *data;
};

static enca_usize
align_up_usize (enca_usize n, enca_usize a)
{
  return (n + a - 1) & ~(a - 1);
}

enca_result
enca_slab_init (enca_slab *s, enca_usize elem_size, enca_usize elem_align,
                enca_usize elems_per_chunk)
{
  if (!s || elem_size == 0)
    return ENCA_ERR_INVALID_ARGUMENT;

  if (elem_align == 0)
    elem_align = ENCA_ALIGNOF (max_align_t);
  if ((elem_align & (elem_align - 1)) != 0 || elem_align > ENCA_SLAB_MAX_ALIGN)
    return ENCA_ERR_INVALID_ARGUMENT;

  if (elems_per_chunk < ENCA_SLAB_MIN_CHUNK_ELEMS)
    elems_per_chunk = ENCA_SLAB_MIN_CHUNK_ELEMS;

  memset (s, 0, sizeof *s);
  s->elem_size = align_up_usize (elem_size, elem_align);
  if (s->elem_size < sizeof (void *))
    s->elem_size = sizeof (void *);
  s->elem_align = elem_align;
  s->elems_per_chunk = elems_per_chunk;
  return ENCA_OK;
}

static void
chunk_populate_freelist (enca_slab *s, enca_slab_chunk *c)
{
  for (enca_usize i = 0; i < s->elems_per_chunk; i++)
    {
      void *elem = c->data + i * s->elem_size;
      void **slot = (void **) elem;
      *slot = s->free_list;
      s->free_list = slot;
    }
}

void *
enca_slab_alloc (enca_slab *s)
{
  ENCA_ASSERT (s != NULL, "null slab");
  ENCA_ASSERT (s->elem_size != 0 && s->elems_per_chunk != 0,
               "slab not initialized");

  if (!s->free_list)
    {
      enca_usize data_size = s->elem_size * s->elems_per_chunk;
      enca_usize raw_size
        = sizeof (enca_slab_chunk) + data_size + s->elem_align;
      enca_slab_chunk *c = chunk_alloc_aligned (raw_size, s->elem_align);
      if (ENCA_UNLIKELY (!c))
        return NULL;

      c->raw = c;
      enca_uptr d = ((enca_uptr) c + sizeof (enca_slab_chunk)
                     + s->elem_align - 1)
                    & ~(enca_uptr) (s->elem_align - 1);
      c->data = (unsigned char *) d;

#ifndef NDEBUG
      memset (c->data, 0xCD, data_size);
#endif
      c->next = s->chunks;
      s->chunks = c;
      s->cap_total += s->elems_per_chunk;
      chunk_populate_freelist (s, c);
    }

  void **slot = (void **) s->free_list;
  void *p = slot;
  s->free_list = *slot;
  s->in_use++;
#ifndef NDEBUG
  memset (p, 0xCD, s->elem_size);
#endif
  return p;
}

#ifndef NDEBUG
static bool
slab_owns_debug (const enca_slab *s, const void *p)
{
  for (const enca_slab_chunk *c = s->chunks; c; c = c->next)
    {
      const unsigned char *begin = c->data;
      const unsigned char *end = begin + s->elem_size * s->elems_per_chunk;
      const unsigned char *q = p;

      if (q >= begin && q < end && (enca_usize) (q - begin) % s->elem_size == 0)
        return true;
    }
  return false;
}
#endif

void
enca_slab_free (enca_slab *s, void *p)
{
  if (!p)
    return;

  ENCA_ASSERT_ALWAYS (s != NULL, "null slab");
#ifndef NDEBUG
  ENCA_ASSERT_ALWAYS (slab_owns_debug (s, p),
                      "pointer not owned by this slab");
#endif

  void **slot = (void **) p;
  *slot = s->free_list;
  s->free_list = slot;
  s->in_use--;
}

void
enca_slab_destroy (enca_slab *s)
{
  ENCA_ASSERT (s != NULL, "null slab");

#ifndef NDEBUG
  ENCA_ASSERT_ALWAYS (s->in_use == 0, "destroying slab with live objects");
#endif

  enca_slab_chunk *c = s->chunks;
  while (c)
    {
      enca_slab_chunk *next = c->next;
      chunk_free_aligned (c);
      c = next;
    }
  memset (s, 0, sizeof *s);
}
