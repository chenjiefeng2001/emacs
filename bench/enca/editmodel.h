#ifndef ENCA_BENCH_EDITMODEL_H
#define ENCA_BENCH_EDITMODEL_H

/* Logical edit-stream model (P2.1.2): every storage candidate
   consumes the same deterministic edit script, so measurements are
   comparable.  Also hosts the shared PRNG and checksum. */

#include "../../src/enca/base/types.h"

#include <stdlib.h>
#include <string.h>

typedef struct enca_edit_rec
{
  enca_u64 position;            /* byte offset (clamped by applier)  */
  enca_u64 delete_len;
  const unsigned char *insert_data;
  enca_usize insert_len;
} enca_edit_rec;

/* Apply an edit to a flat working buffer.  buf must have capacity >=
   size + insert_len; returns the new size. */
static inline enca_usize
enca_edit_apply (unsigned char *buf, enca_usize size,
                 const enca_edit_rec *e)
{
  enca_u64 pos = e->position > size ? size : e->position;
  enca_u64 del = e->delete_len;
  if (del > size - pos)
    del = size - pos;

  if (del != e->insert_len)
    memmove (buf + pos + e->insert_len, buf + pos + del,
             size - pos - del);

  if (e->insert_len)
    memcpy (buf + pos, e->insert_data, e->insert_len);

  return size - (enca_usize) del + (enca_usize) e->insert_len;
}

/* xorshift64* -- deterministic across platforms. */
typedef struct enca_prng { enca_u64 s; } enca_prng;

static inline void
enca_prng_seed (enca_prng *p, enca_u64 seed)
{
  p->s = seed ? seed : 0x9e3779b97f4a7c15ull;
}

static inline enca_u64
enca_prng_next (enca_prng *p)
{
  enca_u64 x = p->s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  p->s = x;
  return x * (enca_u64) 2685821657736338717ull;
}

static inline enca_u64
enca_prng_range (enca_prng *p, enca_u64 n)
{
  return n ? enca_prng_next (p) % n : 0;
}

/* FNV-1a reference checksum (matches runtime worker semantics). */
static inline enca_u64
enca_fnv1a (const unsigned char *p, enca_usize n)
{
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  for (enca_usize i = 0; i < n; i++)
    {
      h ^= p[i];
      h *= (enca_u64) 1099511628211ull;
    }
  return h;
}

#endif
