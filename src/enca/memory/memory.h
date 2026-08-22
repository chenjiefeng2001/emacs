#ifndef ENCA_MEMORY_H
#define ENCA_MEMORY_H

#include "../base/types.h"
#include "../base/attributes.h"

typedef struct enca_mem_stats
{
  enca_u64 alloc_count;
  enca_u64 free_count;
  enca_u64 alloc_bytes;
  enca_u64 free_bytes;
  enca_u64 live_allocs;
  enca_u64 live_bytes;
  enca_u64 peak_live_bytes;
} enca_mem_stats;

void *enca_malloc (enca_usize n);
void *enca_calloc (enca_usize n, enca_usize elem_size);
void *enca_realloc (void *p, enca_usize n);
char *enca_strdup (const char *s);
void enca_free (void *p);

enca_mem_stats enca_mem_stats_snapshot (void);
void enca_mem_stats_reset (void);

#endif
