#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "wake.h"

enca_result
enca_wake_init (enca_wake_source *w)
{
  if (!w)
    return ENCA_ERR_INVALID_ARGUMENT;
  enca_result r = enca_mutex_init (&w->m);
  if (ENCA_RESULT_IS_ERR (r))
    return r;
  r = enca_condition_init (&w->c);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_mutex_destroy (&w->m);
      return r;
    }
  w->token = 0;
  return ENCA_OK;
}

void
enca_wake_destroy (enca_wake_source *w)
{
  if (!w)
    return;
  /* Caller contract: no thread may be inside wait() here. */
  enca_condition_destroy (&w->c);
  enca_mutex_destroy (&w->m);
}

void
enca_wake_notify (enca_wake_source *w)
{
  if (!w)
    return;
  enca_mutex_lock (&w->m);
  if (w->token == 0)
    {
      w->token = 1;             /* IDLE -> NOTIFIED; coalesce otherwise */
      enca_condition_signal (&w->c);
    }
  enca_mutex_unlock (&w->m);
}

bool
enca_wake_wait (enca_wake_source *w, enca_u64 timeout_ns)
{
  if (!w)
    return false;
  enca_mutex_lock (&w->m);
  if (w->token == 0)
    enca_condition_timed_wait (&w->c, &w->m, timeout_ns);
  bool got = w->token != 0;
  if (got)
    w->token = 0;               /* consume; consumer now DRAINING     */
  enca_mutex_unlock (&w->m);
  return got;
}

bool
enca_wake_poll (enca_wake_source *w)
{
  if (!w)
    return false;
  enca_mutex_lock (&w->m);
  bool got = w->token != 0;
  if (got)
    w->token = 0;
  enca_mutex_unlock (&w->m);
  return got;
}
