#include "test_util.h"
#include "../../src/enca/memory/arena.h"

static void
test_basic_alloc_alignment (void)
{
  enca_arena a;
  enca_arena_init (&a, ENCA_KIB (64));

  for (enca_usize align = 1; align <= 64; align <<= 1)
    {
      void *p = enca_arena_alloc_aligned (&a, 7, align);
      CHECK (p != NULL);
      CHECK (((enca_uptr) p & (align - 1)) == 0);
    }

  CHECK_EQ_U64 (enca_arena_total_allocated (&a), 7 * 7);
  enca_arena_destroy (&a);
}

static void
test_growth_multiple_chunks (void)
{
  enca_arena a;
  enca_arena_init (&a, ENCA_KIB (4));

  enum { N = 200 };
  static unsigned char *ptrs[N];
  for (int i = 0; i < N; i++)
    {
      ptrs[i] = enca_arena_alloc (&a, ENCA_KIB (1));
      CHECK (ptrs[i] != NULL);
      memset (ptrs[i], (unsigned char) i, ENCA_KIB (1));
    }

  CHECK (a.chunk_count > 1);

  bool intact = true;
  for (int i = 0; i < N && intact; i++)
    for (int j = 0; j < 8; j++)
      if (ptrs[i][j * 100] != (unsigned char) i)
        {
          intact = false;
          break;
        }
  CHECK (intact);

  enca_arena_destroy (&a);
}

static void
test_reset_reuses_first_chunk (void)
{
  enca_arena a;
  enca_arena_init (&a, ENCA_MIB (1));

  void *p1 = enca_arena_alloc (&a, 1024);
  CHECK (p1 != NULL);
  void *big = enca_arena_alloc (&a, ENCA_MIB (2));
  CHECK (big != NULL);
  CHECK (a.chunk_count >= 2);

  enca_arena_reset (&a);
  CHECK_EQ_U64 (enca_arena_total_allocated (&a), 0);
  CHECK_EQ_U64 (a.chunk_count, 1);

  void *p2 = enca_arena_alloc (&a, 1024);
  CHECK (p2 == p1);

  enca_arena_destroy (&a);
}

static void
test_large_single_alloc (void)
{
  enca_arena a;
  enca_arena_init (&a, ENCA_KIB (4));

  void *huge = enca_arena_alloc (&a, ENCA_MIB (3));
  CHECK (huge != NULL);
  memset (huge, 0x5A, ENCA_MIB (3));

  enca_arena_destroy (&a);
}

void
run_test_arena (void)
{
  enca_test_run_suite ("arena/alignment", test_basic_alloc_alignment);
  enca_test_run_suite ("arena/growth", test_growth_multiple_chunks);
  enca_test_run_suite ("arena/reset", test_reset_reuses_first_chunk);
  enca_test_run_suite ("arena/large", test_large_single_alloc);
}
