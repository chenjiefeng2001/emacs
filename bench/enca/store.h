#ifndef ENCA_BENCH_STORE_H
#define ENCA_BENCH_STORE_H

/* P2.1 storage-study common types.  Experiment-only: lives beside
   the frozen src/enca/snapshot module and mirrors its semantics
   (immutable revisions, refcounted lifetime) without touching it.

   Contract: every candidate consumes the same deterministic edit
   script through publish(prev, edit) -> new immutable revision. */

#include "../../src/enca/base/types.h"
#include "../../src/enca/base/attributes.h"
#include "../../src/enca/diagnostics/error.h"
#include "../../src/enca/time/time.h"
#include "../../src/enca/memory/memory.h"
#include "editmodel.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Per-publish cost account. */
typedef struct enca_rev_metrics
{
  enca_u64 content_copy_bytes;  /* document bytes physically copied  */
  enca_u64 meta_bytes;          /* tables / headers allocated        */
} enca_rev_metrics;

typedef struct enca_bench_store enca_bench_store;
typedef struct enca_bench_rev enca_bench_rev;

typedef struct enca_store_ops
{
  const char *name;             /* "flat" / "chunked"                */

  enca_result (*create) (enca_bench_store **out, enca_usize chunk_size);

  /* Initial immutable revision from raw content. */
  enca_result (*snapshot_init) (enca_bench_store *st,
                                const unsigned char *init,
                                enca_usize n, enca_bench_rev **out,
                                enca_rev_metrics *m);

  /* Derive the next revision from PREV by applying one edit. */
  enca_result (*publish) (enca_bench_store *st, enca_bench_rev *prev,
                          const enca_edit_rec *e, enca_bench_rev **out,
                          enca_rev_metrics *m);

  void (*retain) (enca_bench_rev *rev);
  void (*release) (enca_bench_rev *rev);

  /* Consumers. */
  enca_u64 (*fnv_sequential) (enca_bench_rev *rev);   /* parser scan  */
  enca_u64 (*read_random) (enca_bench_rev *rev, const enca_u64 *offsets,
                           enca_usize n);             /* LSP probes   */

  enca_usize (*rev_len) (enca_bench_rev *rev);
  void (*dump) (enca_bench_rev *rev);   /* optional debug */

  void (*destroy) (enca_bench_store *st);
} enca_store_ops;

struct enca_bench_rev { const enca_store_ops *ops; };
struct enca_bench_store { const enca_store_ops *ops; };

extern const enca_store_ops enca_store_flat_ops;
extern const enca_store_ops enca_store_chunked_ops;

const char *enca_bench_store_family (const enca_bench_store *st);

#endif
