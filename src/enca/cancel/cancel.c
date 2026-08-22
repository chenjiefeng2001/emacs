#include "cancel.h"

#include "../base/assert.h"
#include <stdlib.h>

enca_result
enca_cancel_source_create (enca_cancel_source **out)
{
  if (!out)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_cancel_source *s = calloc (1, sizeof *s);
  if (!s)
    return ENCA_ERR_OUT_OF_MEMORY;

  atomic_store_explicit (&s->refs, 1, memory_order_relaxed);
  atomic_store_explicit (&s->state, 0, memory_order_relaxed);
  s->parent = NULL;
  *out = s;
  return ENCA_OK;
}

enca_cancel_source *
enca_cancel_source_retain (enca_cancel_source *src)
{
  if (!src)
    return NULL;
  atomic_fetch_add_explicit (&src->refs, 1, memory_order_relaxed);
  return src;
}

void
enca_cancel_source_release (enca_cancel_source *src)
{
  if (!src)
    return;

  enca_u32 prev = atomic_fetch_sub_explicit (&src->refs, 1,
                                             memory_order_acq_rel);
  ENCA_ASSERT_ALWAYS (prev > 0, "cancel source refcount underflow");
  if (prev == 1)
    {
      enca_cancel_source *parent = src->parent;
      free (src);
      enca_cancel_source_release (parent);
    }
}

void
enca_cancel_source_cancel (enca_cancel_source *src)
{
  if (ENCA_LIKELY (src))
    atomic_fetch_or_explicit (&src->state, ENCA_CANCEL_STATE_CANCELLED,
                              memory_order_release);
}

enca_result
enca_cancel_child_spawn (enca_cancel_source *parent, enca_cancel_source **out)
{
  if (!out || !parent)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_result r = enca_cancel_source_create (out);
  if (ENCA_RESULT_IS_ERR (r))
    return r;

  (*out)->parent = enca_cancel_source_retain (parent);
  return ENCA_OK;
}
