#ifndef ENCA_CANCEL_H
#define ENCA_CANCEL_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

#include <stdatomic.h>

typedef struct enca_cancel_source
{
  _Atomic enca_u32 refs;
  _Atomic enca_u32 state;
  struct enca_cancel_source *parent;
} enca_cancel_source;

#define ENCA_CANCEL_STATE_CANCELLED ((enca_u32) 1)

enca_result enca_cancel_source_create (enca_cancel_source **out);
enca_cancel_source *enca_cancel_source_retain (enca_cancel_source *src);
void enca_cancel_source_release (enca_cancel_source *src);

void enca_cancel_source_cancel (enca_cancel_source *src);

ENCA_INLINE bool
enca_cancel_source_is_cancelled (const enca_cancel_source *src)
{
  if (!src)
    return false;

  if (atomic_load_explicit (&src->state, memory_order_acquire)
      & ENCA_CANCEL_STATE_CANCELLED)
    return true;

  for (const enca_cancel_source *p = src->parent; p; p = p->parent)
    {
      if (atomic_load_explicit (&p->state, memory_order_acquire)
          & ENCA_CANCEL_STATE_CANCELLED)
        return true;
    }
  return false;
}

enca_result enca_cancel_child_spawn (enca_cancel_source *parent,
                                     enca_cancel_source **out);

#endif
