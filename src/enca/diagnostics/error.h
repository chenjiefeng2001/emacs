#ifndef ENCA_ERROR_H
#define ENCA_ERROR_H

#include "../base/types.h"
#include "../base/attributes.h"

typedef enum enca_result
{
  ENCA_OK = 0,
  ENCA_ERR_INVALID_ARGUMENT = 1,
  ENCA_ERR_OUT_OF_MEMORY = 2,
  ENCA_ERR_CAPACITY = 3,
  ENCA_ERR_TIMEOUT = 4,
  ENCA_ERR_NOT_FOUND = 5,
  ENCA_ERR_WOULD_BLOCK = 6,
  ENCA_ERR_CANCELLED = 7,
  ENCA_ERR_CLOSED = 8,
  ENCA_ERR_INTERNAL = 9,
} enca_result;

#define ENCA_RESULT_IS_OK(r) ((r) == ENCA_OK)
#define ENCA_RESULT_IS_ERR(r) ((r) != ENCA_OK)

ENCA_NODISCARD const char *enca_result_str (enca_result r);

typedef void (*enca_panic_fn) (const char *file, int line, const char *msg,
                               void *user_data);

void enca_set_panic_handler (enca_panic_fn handler, void *user_data);

#endif
