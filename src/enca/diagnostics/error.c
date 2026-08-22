#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enca_panic_fn enca_panic_handler;
static void *enca_panic_user_data;

const char *
enca_result_str (enca_result r)
{
  switch (r)
    {
    case ENCA_OK:
      return "ok";
    case ENCA_ERR_INVALID_ARGUMENT:
      return "invalid-argument";
    case ENCA_ERR_OUT_OF_MEMORY:
      return "out-of-memory";
    case ENCA_ERR_CAPACITY:
      return "capacity-exceeded";
    case ENCA_ERR_TIMEOUT:
      return "timeout";
    case ENCA_ERR_NOT_FOUND:
      return "not-found";
    case ENCA_ERR_WOULD_BLOCK:
      return "would-block";
    case ENCA_ERR_CANCELLED:
      return "cancelled";
    case ENCA_ERR_CLOSED:
      return "closed";
    case ENCA_ERR_INTERNAL:
      return "internal-error";
    }
  return "unknown-result";
}

void
enca_set_panic_handler (enca_panic_fn handler, void *user_data)
{
  enca_panic_handler = handler;
  enca_panic_user_data = user_data;
}

void
enca_panic (const char *file, int line, const char *fmt, ...)
{
  char msg[512];
  va_list ap;

  va_start (ap, fmt);
  vsnprintf (msg, sizeof msg, fmt, ap);
  va_end (ap);

  if (enca_panic_handler)
    {
      enca_panic_handler (file, line, msg, enca_panic_user_data);
      abort ();
    }

  fprintf (stderr, "enca panic: %s:%d: %s\n", file, line, msg);
  fflush (stderr);
  abort ();
}
