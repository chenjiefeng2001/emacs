#include "test_util.h"
#include "../../src/enca/base/types.h"
#include "../../src/enca/base/attributes.h"
#include "../../src/enca/base/assert.h"
#include "../../src/enca/base/version.h"
#include "../../src/enca/diagnostics/error.h"

ENCA_STATIC_ASSERT (sizeof (enca_u64) == 8, "u64");
ENCA_STATIC_ASSERT (sizeof (enca_object_id) == 8, "id");
ENCA_STATIC_ASSERT (sizeof (enca_timestamp_ns) == 8, "timestamp");
ENCA_STATIC_ASSERT (sizeof (enca_flags_t) == 4, "flags");

static void
test_result_strings (void)
{
  CHECK (enca_result_str (ENCA_OK) != NULL);
  CHECK (enca_result_str (ENCA_ERR_OUT_OF_MEMORY) != NULL);
  CHECK (enca_result_str ((enca_result) 999) != NULL);
  CHECK_EQ_U64 (ENCA_RESULT_IS_OK (ENCA_OK), 1);
  CHECK_EQ_U64 (ENCA_RESULT_IS_ERR (ENCA_ERR_TIMEOUT), 1);
}

void
run_test_base (void)
{
  enca_test_run_suite ("base", test_result_strings);
}
