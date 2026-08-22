#include "memory.h"

#include "../base/assert.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define ENCA_MEM_MAGIC ((enca_u32) 0xE4CA10C0u)

typedef struct enca_mem_header
{
  enca_u32 magic;
  enca_usize size;
  max_align_t align[];
} enca_mem_header;

#define ENCA_MEM_HEADER_SIZE \
  ((sizeof (enca_mem_header) + ENCA_ALIGNOF (max_align_t) - 1) \
   & ~(ENCA_ALIGNOF (max_align_t) - 1))

static _Atomic enca_u64 mem_alloc_count;
static _Atomic enca_u64 mem_free_count;
static _Atomic enca_u64 mem_alloc_bytes;
static _Atomic enca_u64 mem_free_bytes;
static _Atomic enca_u64 mem_live_bytes;
static _Atomic enca_u64 mem_peak_live_bytes;

static void
peak_update (enca_u64 live)
{
  enca_u64 cur = atomic_load_explicit (&mem_peak_live_bytes,
                                       memory_order_relaxed);

  while (live > cur
         && !atomic_compare_exchange_weak_explicit (&mem_peak_live_bytes, &cur,
                                                    live, memory_order_relaxed,
                                                    memory_order_relaxed))
    ;
}

#ifndef NDEBUG
# define ENCA_MEM_POISON_NEW(p, n) memset ((p), 0xCD, (n))
# define ENCA_MEM_POISON_FREED(p, n) memset ((p), 0xDD, (n))
#else
# define ENCA_MEM_POISON_NEW(p, n) ((void) 0)
# define ENCA_MEM_POISON_FREED(p, n) ((void) 0)
#endif

void *
enca_malloc (enca_usize n)
{
  if (n == 0)
    return NULL;

  if (ENCA_UNLIKELY (n > SIZE_MAX - ENCA_MEM_HEADER_SIZE))
    return NULL;

  unsigned char *raw = malloc (n + ENCA_MEM_HEADER_SIZE);
  if (ENCA_UNLIKELY (!raw))
    return NULL;

  enca_mem_header *h = (enca_mem_header *) raw;
  h->magic = ENCA_MEM_MAGIC;
  h->size = n;
  void *user = raw + ENCA_MEM_HEADER_SIZE;
  ENCA_MEM_POISON_NEW (user, n);

  atomic_fetch_add_explicit (&mem_alloc_count, 1, memory_order_relaxed);
  atomic_fetch_add_explicit (&mem_alloc_bytes, n, memory_order_relaxed);
  atomic_fetch_add_explicit (&mem_live_bytes, n, memory_order_relaxed);
  peak_update (atomic_load_explicit (&mem_live_bytes, memory_order_relaxed));
  return user;
}

void *
enca_calloc (enca_usize n, enca_usize elem_size)
{
  if (n != 0 && elem_size != 0 && n > SIZE_MAX / elem_size)
    return NULL;

  enca_usize total = n * elem_size;
  void *p = enca_malloc (total);
  if (p)
    memset (p, 0, total);
  return p;
}

void *
enca_realloc (void *p, enca_usize n)
{
  if (!p)
    return enca_malloc (n);
  if (n == 0)
    {
      enca_free (p);
      return NULL;
    }

  unsigned char *raw = (unsigned char *) p - ENCA_MEM_HEADER_SIZE;
  enca_mem_header *h = (enca_mem_header *) raw;
  ENCA_ASSERT_ALWAYS (h->magic == ENCA_MEM_MAGIC,
                      "realloc on pointer not from enca allocator");

  enca_usize old = h->size;

  if (n > SIZE_MAX - ENCA_MEM_HEADER_SIZE)
    return NULL;

  unsigned char *nraw = realloc (raw, n + ENCA_MEM_HEADER_SIZE);
  if (ENCA_UNLIKELY (!nraw))
    return NULL;

  h = (enca_mem_header *) nraw;
  h->magic = ENCA_MEM_MAGIC;
  h->size = n;
  void *user = nraw + ENCA_MEM_HEADER_SIZE;

  if (n > old)
    ENCA_MEM_POISON_NEW ((unsigned char *) user + old, n - old);

  enca_i64 delta = (enca_i64) n - (enca_i64) old;
  atomic_fetch_add_explicit (&mem_alloc_bytes, (enca_u64) (delta > 0 ? delta : 0),
                             memory_order_relaxed);
  atomic_fetch_add_explicit (&mem_free_bytes, (enca_u64) (delta < 0 ? -delta : 0),
                             memory_order_relaxed);
  atomic_fetch_add_explicit (&mem_live_bytes, delta, memory_order_relaxed);
  peak_update (atomic_load_explicit (&mem_live_bytes, memory_order_relaxed));
  return user;
}

char *
enca_strdup (const char *s)
{
  if (!s)
    return NULL;
  enca_usize n = strlen (s) + 1;
  char *p = enca_malloc (n);
  if (p)
    memcpy (p, s, n);
  return p;
}

void
enca_free (void *p)
{
  if (!p)
    return;

  unsigned char *raw = (unsigned char *) p - ENCA_MEM_HEADER_SIZE;
  enca_mem_header *h = (enca_mem_header *) raw;
  ENCA_ASSERT_ALWAYS (h->magic == ENCA_MEM_MAGIC,
                      "free on pointer not from enca allocator");
  h->magic = 0;

  ENCA_MEM_POISON_FREED (p, h->size);
  atomic_fetch_add_explicit (&mem_free_count, 1, memory_order_relaxed);
  atomic_fetch_add_explicit (&mem_free_bytes, h->size, memory_order_relaxed);
  atomic_fetch_sub_explicit (&mem_live_bytes, h->size, memory_order_relaxed);
  free (raw);
}

enca_mem_stats
enca_mem_stats_snapshot (void)
{
  enca_mem_stats s;
  s.alloc_count = atomic_load_explicit (&mem_alloc_count, memory_order_relaxed);
  s.free_count = atomic_load_explicit (&mem_free_count, memory_order_relaxed);
  s.alloc_bytes = atomic_load_explicit (&mem_alloc_bytes, memory_order_relaxed);
  s.free_bytes = atomic_load_explicit (&mem_free_bytes, memory_order_relaxed);
  s.live_bytes = atomic_load_explicit (&mem_live_bytes, memory_order_relaxed);
  s.peak_live_bytes
    = atomic_load_explicit (&mem_peak_live_bytes, memory_order_relaxed);
  s.live_allocs = s.alloc_count >= s.free_count ? s.alloc_count - s.free_count : 0;
  return s;
}

void
enca_mem_stats_reset (void)
{
  atomic_store_explicit (&mem_alloc_count, 0, memory_order_relaxed);
  atomic_store_explicit (&mem_free_count, 0, memory_order_relaxed);
  atomic_store_explicit (&mem_alloc_bytes, 0, memory_order_relaxed);
  atomic_store_explicit (&mem_free_bytes, 0, memory_order_relaxed);
  atomic_store_explicit (&mem_live_bytes,
                         atomic_load_explicit (&mem_live_bytes,
                                               memory_order_relaxed),
                         memory_order_relaxed);
}
