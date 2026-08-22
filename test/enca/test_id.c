#include "test_util.h"
#include "../../src/enca/id/id.h"

static void
test_id_layout (void)
{
  enca_object_id id = enca_id_make (ENCA_OBJ_BUFFER, 42, 1000);

  CHECK_EQ_U64 (enca_id_type (id), ENCA_OBJ_BUFFER);
  CHECK_EQ_U64 (enca_id_gen (id), 42);
  CHECK_EQ_U64 (enca_id_index (id), 1000);
  CHECK (enca_id_valid (id));
  CHECK (!enca_id_valid (ENCA_INVALID_ID));
  CHECK (!enca_id_valid (0));

  enca_u32 max_gen = ENCA_ID_GEN_MAX;
  id = enca_id_make (ENCA_OBJ_TASK, max_gen, UINT32_MAX);
  CHECK_EQ_U64 (enca_id_gen (id), max_gen);
  CHECK_EQ_U64 (enca_id_index (id), UINT32_MAX);
}

static void
test_registry_lifecycle (void)
{
  enca_id_registry reg;
  CHECK_EQ_U64 (enca_idr_init (&reg), ENCA_OK);

  enca_object_id a = ENCA_INVALID_ID;
  enca_object_id b = ENCA_INVALID_ID;

  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_BUFFER, &a), ENCA_OK);
  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_BUFFER, &b), ENCA_OK);
  CHECK (a != b);
  CHECK (enca_idr_is_alive (&reg, a));
  CHECK (enca_idr_is_alive (&reg, b));
  CHECK_EQ_U64 (enca_idr_live_count (&reg), 2);

  CHECK_EQ_U64 (enca_idr_free (&reg, a), ENCA_OK);
  CHECK (!enca_idr_is_alive (&reg, a));
  CHECK (enca_idr_is_alive (&reg, b));
  CHECK_EQ_U64 (enca_idr_live_count (&reg), 1);

  CHECK_EQ_U64 (enca_idr_free (&reg, a), ENCA_ERR_NOT_FOUND);

  enca_object_id c = ENCA_INVALID_ID;
  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_BUFFER, &c), ENCA_OK);
  CHECK_EQ_U64 (enca_id_index (c), enca_id_index (a));
  CHECK (enca_id_gen (c) != enca_id_gen (a));
  CHECK (c != a);

  enca_idr_destroy (&reg);
}

static void
test_registry_types_independent (void)
{
  enca_id_registry reg;
  enca_idr_init (&reg);

  enca_object_id buf = ENCA_INVALID_ID;
  enca_object_id win = ENCA_INVALID_ID;

  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_BUFFER, &buf), ENCA_OK);
  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_WINDOW, &win), ENCA_OK);
  CHECK_EQ_U64 (enca_id_type (buf), ENCA_OBJ_BUFFER);
  CHECK_EQ_U64 (enca_id_type (win), ENCA_OBJ_WINDOW);
  CHECK_EQ_U64 (enca_id_index (buf), enca_id_index (win));

  CHECK_EQ_U64 (enca_idr_free (&reg, buf), ENCA_OK);
  CHECK (enca_idr_is_alive (&reg, win));
  CHECK (!enca_idr_is_alive (&reg, buf));

  enca_idr_destroy (&reg);
}

static void
test_registry_invalid_args (void)
{
  enca_id_registry reg;
  enca_idr_init (&reg);

  enca_object_id id = ENCA_INVALID_ID;
  CHECK (ENCA_RESULT_IS_ERR (enca_idr_alloc (&reg, ENCA_OBJ_NONE, &id)));
  CHECK (ENCA_RESULT_IS_ERR (enca_idr_alloc (&reg, ENCA_OBJ_BUFFER, NULL)));
  CHECK (ENCA_RESULT_IS_ERR (enca_idr_alloc (NULL, ENCA_OBJ_BUFFER, &id)));
  CHECK (ENCA_RESULT_IS_ERR (enca_idr_free (&reg, ENCA_INVALID_ID)));
  CHECK (!enca_idr_is_alive (&reg, ENCA_INVALID_ID));
  CHECK (!enca_idr_is_alive (&reg, enca_id_make (200, 1, 1)));

  enca_idr_destroy (&reg);
}

static void
test_many_allocs (void)
{
  enum { N = 5000 };
  enca_id_registry reg;
  enca_idr_init (&reg);

  static enca_object_id ids[N];
  for (int i = 0; i < N; i++)
    {
      CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_EVENT_SOURCE, &ids[i]),
                    ENCA_OK);
      if (enca_test_failures)
        break;
    }

  CHECK_EQ_U64 (enca_idr_live_count (&reg), N);

  enca_object_id last_freed = ENCA_INVALID_ID;
  enca_usize freed = 0;
  for (int i = 0; i < N; i += 7)
    {
      if (ENCA_RESULT_IS_OK (enca_idr_free (&reg, ids[i])))
        {
          last_freed = ids[i];
          ids[i] = ENCA_INVALID_ID;
          freed++;
        }
    }

  enca_object_id recycled = ENCA_INVALID_ID;
  CHECK_EQ_U64 (enca_idr_alloc (&reg, ENCA_OBJ_EVENT_SOURCE, &recycled),
                ENCA_OK);
  CHECK_EQ_U64 (enca_id_index (recycled), enca_id_index (last_freed));
  CHECK (recycled != last_freed);
  CHECK_EQ_U64 (enca_idr_live_count (&reg),
                (unsigned long long) N - freed + 1);

  enca_idr_destroy (&reg);
}

void
run_test_id (void)
{
  enca_test_run_suite ("id/layout", test_id_layout);
  enca_test_run_suite ("id/lifecycle", test_registry_lifecycle);
  enca_test_run_suite ("id/types", test_registry_types_independent);
  enca_test_run_suite ("id/invalid-args", test_registry_invalid_args);
  enca_test_run_suite ("id/many", test_many_allocs);
}
