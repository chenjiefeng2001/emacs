#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "dispatch.h"
#include "../queue/blocking_queue.h"
#include "../queue/spsc_ring.h"

enca_usize
enca_event_dispatch_spsc (enca_spsc_ring *q, enca_event_handler handler,
                          void *ctx, enca_usize max_events)
{
  enca_event e;
  enca_usize n = 0;

  while (n < max_events && enca_spsc_try_pop (q, &e))
    {
      handler (&e, ctx);
      n++;
    }
  return n;
}

enca_result
enca_event_dispatch_blocking (enca_blocking_queue *q, enca_event_handler handler,
                              void *ctx, enca_deadline deadline)
{
  for (;;)
    {
      enca_event e;
      enca_result r = enca_bq_pop (q, &e, deadline);

      if (r == ENCA_ERR_TIMEOUT)
        return ENCA_OK;
      if (ENCA_RESULT_IS_ERR (r))
        return r;

      enca_result hr = handler (&e, ctx);
      if (ENCA_RESULT_IS_ERR (hr))
        return hr;
    }
}
