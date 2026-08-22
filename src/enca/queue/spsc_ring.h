#ifndef ENCA_SPSC_RING_H
#define ENCA_SPSC_RING_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../event/event.h"

#include <stdatomic.h>

typedef struct enca_spsc_ring
{
  enca_event *slots;
  enca_u32 capacity;
  enca_u64 mask;
  _Atomic enca_u64 head;
  _Atomic enca_u64 tail;
} enca_spsc_ring;

enca_result enca_spsc_init (enca_spsc_ring *r, enca_u32 capacity_pow2);
void enca_spsc_destroy (enca_spsc_ring *r);

bool enca_spsc_try_push (enca_spsc_ring *r, const enca_event *e);
bool enca_spsc_try_pop (enca_spsc_ring *r, enca_event *out);

ENCA_INLINE enca_u64
enca_spsc_size (const enca_spsc_ring *r)
{
  enca_u64 t = atomic_load_explicit (&r->tail, memory_order_acquire);
  enca_u64 h = atomic_load_explicit (&r->head, memory_order_acquire);
  return t - h;
}

#endif
