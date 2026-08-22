#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "spsc_ring.h"

#include "../base/assert.h"
#include "../time/time.h"
#include <stdlib.h>
#include <string.h>

static enca_u32
round_pow2 (enca_u32 v)
{
  enca_u32 r = 1;
  while (r < v)
    r <<= 1;
  return r;
}

enca_result
enca_spsc_init (enca_spsc_ring *r, enca_u32 capacity_pow2)
{
  if (!r || capacity_pow2 == 0)
    return ENCA_ERR_INVALID_ARGUMENT;

  memset ((void *) r, 0, sizeof *r);
  r->capacity = round_pow2 (capacity_pow2);
  r->mask = r->capacity - 1u;
  r->slots = calloc (r->capacity, sizeof (enca_event));
  if (!r->slots)
    {
      r->capacity = 0;
      r->mask = 0;
      return ENCA_ERR_OUT_OF_MEMORY;
    }
  atomic_store_explicit (&r->head, 0, memory_order_relaxed);
  atomic_store_explicit (&r->tail, 0, memory_order_relaxed);
  return ENCA_OK;
}

void
enca_spsc_destroy (enca_spsc_ring *r)
{
  if (!r)
    return;
  free (r->slots);
  memset ((void *) r, 0, sizeof *r);
}

bool
enca_spsc_try_push (enca_spsc_ring *r, const enca_event *e)
{
  ENCA_ASSERT (r != NULL && e != NULL, "null spsc ring or event");

  enca_u64 tail = atomic_load_explicit (&r->tail, memory_order_relaxed);
  enca_u64 head = atomic_load_explicit (&r->head, memory_order_acquire);

  if (tail - head >= r->capacity)
    return false;

  r->slots[tail & r->mask] = *e;
  atomic_store_explicit (&r->tail, tail + 1, memory_order_release);
  return true;
}

bool
enca_spsc_try_pop (enca_spsc_ring *r, enca_event *out)
{
  ENCA_ASSERT (r != NULL && out != NULL, "null spsc ring or out");

  enca_u64 head = atomic_load_explicit (&r->head, memory_order_relaxed);
  enca_u64 tail = atomic_load_explicit (&r->tail, memory_order_acquire);

  if (head >= tail)
    return false;

  *out = r->slots[head & r->mask];
  atomic_store_explicit (&r->head, head + 1, memory_order_release);
  return true;
}
