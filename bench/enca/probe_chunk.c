#include "store.h"
#include <stdio.h>

int
main (void)
{
  enca_bench_store *st = NULL;
  if (enca_store_chunked_ops.create (&st, 64) != ENCA_OK)
    return 1;

  unsigned char init[100];
  for (int i = 0; i < 100; i++)
    init[i] = (unsigned char) i;
  enca_rev_metrics m;
  enca_bench_rev *cur = NULL;
  if (enca_store_chunked_ops.snapshot_init (st, init, 100, &cur, &m)
      != ENCA_OK)
    return 1;
  printf ("init len=%zu fnv=%llx\n",
          (size_t) enca_store_chunked_ops.rev_len (cur),
          (unsigned long long) enca_store_chunked_ops.fnv_sequential (cur));

  unsigned char ins[] = "XY";
  enca_edit_rec e = { 50, 0, ins, 2 };
  enca_bench_rev *r2 = NULL;
  if (enca_store_chunked_ops.publish (st, cur, &e, &r2, &m) != ENCA_OK)
    return 1;
  printf ("after ins@50: len=%zu copied=%llu\n",
          (size_t) enca_store_chunked_ops.rev_len (r2),
          (unsigned long long) m.content_copy_bytes);

  enca_edit_rec e2 = { 10, 5, NULL, 0 };
  enca_bench_rev *r3 = NULL;
  if (enca_store_chunked_ops.publish (st, r2, &e2, &r3, &m) != ENCA_OK)
    return 1;
  printf ("after del@10x5: len=%zu\n",
          (size_t) enca_store_chunked_ops.rev_len (r3));

  /* Reference model */
  unsigned char ref[128];
  memcpy (ref, init, 100);
  enca_usize rl = 100;
  rl = enca_edit_apply (ref, rl, &e);
  rl = enca_edit_apply (ref, rl, &e2);
  printf ("reference len=%zu fnv=%llx\n", (size_t) rl,
          (unsigned long long) enca_fnv1a (ref, rl));
  printf ("store r3 fnv=%llx\n",
          (unsigned long long) enca_store_chunked_ops.fnv_sequential (r3));

  /* Random reads */
  enca_u64 offs[4] = { 0, 30, 60, 96 };
  printf ("random=%llx ref=%llx\n",
          (unsigned long long)
            enca_store_chunked_ops.read_random (r3, offs, 4),
          (unsigned long long) (ref[0] + ref[30] + ref[60] + ref[96]));

  enca_store_chunked_ops.release (r3);
  enca_store_chunked_ops.release (r2);
  enca_store_chunked_ops.release (cur);
  enca_store_chunked_ops.destroy (st);
  printf ("done\n");
  return 0;
}
