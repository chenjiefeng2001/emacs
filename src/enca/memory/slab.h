#ifndef ENCA_SLAB_H
#define ENCA_SLAB_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

#define ENCA_SLAB_MIN_CHUNK_ELEMS 16

typedef struct enca_slab_chunk enca_slab_chunk;

typedef struct enca_slab
{
  enca_usize elem_size;
  enca_usize elem_align;
  enca_usize elems_per_chunk;
  void *free_list;
  enca_slab_chunk *chunks;
  enca_usize cap_total;
  enca_usize in_use;
} enca_slab;

enca_result enca_slab_init (enca_slab *s, enca_usize elem_size,
                            enca_usize elem_align,
                            enca_usize elems_per_chunk);
void *enca_slab_alloc (enca_slab *s);
void enca_slab_free (enca_slab *s, void *p);
void enca_slab_destroy (enca_slab *s);

ENCA_INLINE enca_usize
enca_slab_in_use (const enca_slab *s)
{
  return s ? s->in_use : 0;
}

ENCA_INLINE enca_usize
enca_slab_capacity (const enca_slab *s)
{
  return s ? s->cap_total : 0;
}

#endif
