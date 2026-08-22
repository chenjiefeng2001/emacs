#ifndef ENCA_ARENA_H
#define ENCA_ARENA_H

#include "../base/types.h"
#include "../base/attributes.h"

typedef struct enca_arena_chunk enca_arena_chunk;

typedef struct enca_arena
{
  enca_arena_chunk *head;
  enca_usize default_chunk_size;
  enca_usize total_allocated;
  unsigned chunk_count;
} enca_arena;

#define ENCA_ARENA_MIN_CHUNK ENCA_KIB (4)

void enca_arena_init (enca_arena *a, enca_usize chunk_size_hint);
void *enca_arena_alloc_aligned (enca_arena *a, enca_usize n,
                                enca_usize alignment);
void *enca_arena_alloc (enca_arena *a, enca_usize n);
void enca_arena_reset (enca_arena *a);
void enca_arena_destroy (enca_arena *a);

ENCA_INLINE enca_usize
enca_arena_total_allocated (const enca_arena *a)
{
  return a ? a->total_allocated : 0;
}

#endif
