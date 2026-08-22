#include "test_util.h"
#include "../../src/enca/memory/slab.h"

static void
test_basic_cycle (void)
{
  enca_slab s;
  CHECK_EQ_U64 (enca_slab_init (&s, 32, 0, 16), ENCA_OK);
  CHECK_EQ_U64 (s.elem_size, sizeof (void *) > 32 ? sizeof (void *) : 32);

  void *p1 = enca_slab_alloc (&s);
  void *p2 = enca_slab_alloc (&s);
  CHECK (p1 && p2 && p1 != p2);
  CHECK_EQ_U64 (enca_slab_in_use (&s), 2);
  CHECK_EQ_U64 (enca_slab_capacity (&s), ENCA_SLAB_MIN_CHUNK_ELEMS);

  enca_slab_free (&s, p1);
  enca_slab_free (&s, p2);
  CHECK_EQ_U64 (enca_slab_in_use (&s), 0);

  void *p3 = enca_slab_alloc (&s);
  CHECK (p3 == p2 || p3 == p1);
  CHECK_EQ_U64 (enca_slab_in_use (&s), 1);

  enca_slab_free (&s, p3);
  CHECK_EQ_U64 (enca_slab_in_use (&s), 0);

  enca_slab_destroy (&s);
}

static void
test_growth (void)
{
  enum { N = 100 };
  enca_slab s;
  enca_slab_init (&s, 24, 8, 8);

  static void *ptrs[N];
  for (int i = 0; i < N; i++)
    {
      ptrs[i] = enca_slab_alloc (&s);
      CHECK (ptrs[i] != NULL);
      memset (ptrs[i], 0x11, 24);
    }
  CHECK_EQ_U64 (enca_slab_in_use (&s), N);
  CHECK (enca_slab_capacity (&s) >= N);

  for (int i = 0; i < N; i += 3)
    {
      enca_slab_free (&s, ptrs[i]);
      ptrs[i] = NULL;
    }
  CHECK_EQ_U64 (enca_slab_in_use (&s), N - (N + 2) / 3);

  bool ok = true;
  for (int i = 0; i < N && ok; i++)
    if (ptrs[i] && ((unsigned char *) ptrs[i])[0] != 0x11)
      ok = false;
  CHECK (ok);

  for (int i = 0; i < N; i++)
    {
      enca_slab_free (&s, ptrs[i]);
      ptrs[i] = NULL;
    }
  CHECK_EQ_U64 (enca_slab_in_use (&s), 0);

  enca_slab_destroy (&s);
}

static void
test_alignment (void)
{
  enca_slab s;
  CHECK_EQ_U64 (enca_slab_init (&s, 20, 16, 32), ENCA_OK);

  enum { NA = 10 };
  void *ptrs[NA];
  for (int i = 0; i < NA; i++)
    {
      ptrs[i] = enca_slab_alloc (&s);
      CHECK (ptrs[i] != NULL);
      CHECK (((enca_uptr) ptrs[i] & 15u) == 0);
    }

  for (int i = 0; i < NA; i++)
    enca_slab_free (&s, ptrs[i]);
  CHECK_EQ_U64 (enca_slab_in_use (&s), 0);

  enca_slab_destroy (&s);
}

static void
test_invalid_init (void)
{
  enca_slab s;
  CHECK (ENCA_RESULT_IS_ERR (enca_slab_init (&s, 0, 8, 16)));
  CHECK (ENCA_RESULT_IS_ERR (enca_slab_init (&s, 16, 3, 16)));
  CHECK (ENCA_RESULT_IS_ERR (enca_slab_init (&s, 16, 8192, 16)));
  CHECK (ENCA_RESULT_IS_ERR (enca_slab_init (NULL, 16, 8, 16)));
  CHECK_EQ_U64 (enca_slab_init (&s, 16, 512, 16), ENCA_OK);
}

static void
test_foreign_free_panics (void)
{
#if defined(ENCA_TEST_ASAN) || defined(NDEBUG)
  return;
#else
  enca_slab s;
  enca_slab_init (&s, 32, 0, 16);

  int foreign;

  ENCA_TEST_EXPECT_PANIC ({ enca_slab_free (&s, &foreign); });

  enca_slab_destroy (&s);
#endif
}

void
run_test_slab (void)
{
  enca_test_run_suite ("slab/basic", test_basic_cycle);
  enca_test_run_suite ("slab/growth", test_growth);
  enca_test_run_suite ("slab/alignment", test_alignment);
  enca_test_run_suite ("slab/invalid-init", test_invalid_init);
  enca_test_run_suite ("slab/foreign-free", test_foreign_free_panics);
}
