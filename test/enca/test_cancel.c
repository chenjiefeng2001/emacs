#include "test_util.h"
#include "../../src/enca/cancel/cancel.h"

static void
test_basic_cancel (void)
{
  enca_cancel_source *s = NULL;
  CHECK_EQ_U64 (enca_cancel_source_create (&s), ENCA_OK);
  CHECK (!enca_cancel_source_is_cancelled (s));

  enca_cancel_source_cancel (s);
  CHECK (enca_cancel_source_is_cancelled (s));
  enca_cancel_source_release (s);

  CHECK (!enca_cancel_source_is_cancelled (NULL));
}

static void
test_child_sees_parent_cancel (void)
{
  enca_cancel_source *parent = NULL;
  enca_cancel_source_create (&parent);

  enca_cancel_source *child = NULL;
  CHECK_EQ_U64 (enca_cancel_child_spawn (parent, &child), ENCA_OK);

  CHECK (!enca_cancel_source_is_cancelled (child));
  enca_cancel_source_cancel (parent);
  CHECK (enca_cancel_source_is_cancelled (child));

  enca_cancel_source_release (child);
  enca_cancel_source_release (parent);
}

static void
test_parent_does_not_see_child_cancel (void)
{
  enca_cancel_source *parent = NULL;
  enca_cancel_source_create (&parent);

  enca_cancel_source *child = NULL;
  enca_cancel_child_spawn (parent, &child);

  enca_cancel_source_cancel (child);
  CHECK (enca_cancel_source_is_cancelled (child));
  CHECK (!enca_cancel_source_is_cancelled (parent));

  enca_cancel_source_release (child);
  enca_cancel_source_release (parent);
}

static void
test_grandchild_chain (void)
{
  enca_cancel_source *root = NULL;
  enca_cancel_source_create (&root);

  enca_cancel_source *mid = NULL, *leaf = NULL;
  enca_cancel_child_spawn (root, &mid);
  enca_cancel_child_spawn (mid, &leaf);

  CHECK (!enca_cancel_source_is_cancelled (leaf));
  enca_cancel_source_cancel (root);
  CHECK (enca_cancel_source_is_cancelled (mid));
  CHECK (enca_cancel_source_is_cancelled (leaf));

  enca_cancel_source_release (leaf);
  enca_cancel_source_release (mid);
  enca_cancel_source_release (root);
}

static void
test_retain_release_cycle (void)
{
  enca_cancel_source *s = NULL;
  enca_cancel_source_create (&s);

  CHECK (enca_cancel_source_retain (s) == s);
  enca_cancel_source_release (s);

  enca_cancel_source_cancel (s);
  CHECK (enca_cancel_source_is_cancelled (s));
  enca_cancel_source_release (s);
}

static void
test_invalid_args (void)
{
  CHECK (ENCA_RESULT_IS_ERR (enca_cancel_source_create (NULL)));
  CHECK (ENCA_RESULT_IS_ERR (enca_cancel_child_spawn (NULL, NULL)));

  enca_cancel_source *out = NULL;
  CHECK (ENCA_RESULT_IS_ERR (enca_cancel_child_spawn (NULL, &out)));
}

void
run_test_cancel (void)
{
  enca_test_run_suite ("cancel/basic", test_basic_cancel);
  enca_test_run_suite ("cancel/child-parent", test_child_sees_parent_cancel);
  enca_test_run_suite ("cancel/parent-independent",
                       test_parent_does_not_see_child_cancel);
  enca_test_run_suite ("cancel/grandchild", test_grandchild_chain);
  enca_test_run_suite ("cancel/refcount", test_retain_release_cycle);
  enca_test_run_suite ("cancel/invalid", test_invalid_args);
}
