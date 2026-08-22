#include "test_util.h"
#include "../../src/enca/memory/memory.h"

static void
test_alloc_free_stats (void)
{
  enca_mem_stats_reset ();
  void *p = enca_malloc (128);

  CHECK (p != NULL);
  enca_mem_stats s = enca_mem_stats_snapshot ();
  CHECK_EQ_U64 (s.alloc_count, 1);
  CHECK_EQ_U64 (s.live_bytes, 128);
  CHECK_EQ_U64 (s.peak_live_bytes, 128);

  enca_free (p);
  s = enca_mem_stats_snapshot ();
  CHECK_EQ_U64 (s.free_count, 1);
  CHECK_EQ_U64 (s.live_bytes, 0);

  enca_free (NULL);
  s = enca_mem_stats_snapshot ();
  CHECK_EQ_U64 (s.free_count, 1);
}

static void
test_calloc_zeroed (void)
{
  unsigned char *p = enca_calloc (16, 16);
  CHECK (p != NULL);

  bool all_zero = true;
  for (int i = 0; i < 256; i++)
    all_zero = all_zero && p[i] == 0;
  CHECK (all_zero);
  enca_free (p);
}

static void
test_realloc_preserves (void)
{
  unsigned char *p = enca_malloc (32);
  CHECK (p != NULL);
  memset (p, 0xAB, 32);

  p = enca_realloc (p, 128);
  CHECK (p != NULL);
  bool preserved = true;
  for (int i = 0; i < 32; i++)
    preserved = preserved && p[i] == 0xAB;
  CHECK (preserved);

  p = enca_realloc (p, 8);
  CHECK (p != NULL);
  enca_free (p);

  CHECK (enca_realloc (NULL, 64) != NULL);
  enca_free (enca_malloc (0) ? NULL : NULL);
}

static void
test_strdup (void)
{
  char *s = enca_strdup ("hello");
  CHECK (s != NULL);
  CHECK (strcmp (s, "hello") == 0);
  enca_free (s);
  CHECK (enca_strdup (NULL) == NULL);
}

static void
test_double_free_panics (void)
{
#ifdef ENCA_TEST_ASAN
  return;
#endif
  void *p = enca_malloc (16);
  CHECK (p != NULL);
  enca_free (p);

  ENCA_TEST_EXPECT_PANIC ({ enca_free (p); });
}

static void
test_foreign_pointer_panics (void)
{
#ifdef ENCA_TEST_ASAN
  return;
#endif
  int local = 42;

  ENCA_TEST_EXPECT_PANIC ({ enca_realloc (&local, 32); });
}

void
run_test_memory (void)
{
  enca_test_run_suite ("memory/stats", test_alloc_free_stats);
  enca_test_run_suite ("memory/calloc", test_calloc_zeroed);
  enca_test_run_suite ("memory/realloc", test_realloc_preserves);
  enca_test_run_suite ("memory/strdup", test_strdup);
  enca_test_run_suite ("memory/double-free", test_double_free_panics);
  enca_test_run_suite ("memory/foreign-ptr", test_foreign_pointer_panics);
}
