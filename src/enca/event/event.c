#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "event.h"

#include "../time/time.h"
#include <stdatomic.h>

static _Atomic enca_seq_t event_sequence;

enca_seq_t
enca_event_next_sequence (void)
{
  return atomic_fetch_add_explicit (&event_sequence, 1, memory_order_relaxed);
}

void
enca_event_init (enca_event *e, enca_event_type type, enca_object_id source,
                 enca_flags_t flags, void *payload)
{
  e->type = type;
  e->source = source;
  e->sequence = enca_event_next_sequence ();
  e->timestamp = enca_monotonic_now_ns ();
  e->flags = flags;
  e->payload = payload;
}

const char *
enca_event_type_str (enca_event_type type)
{
  switch (type)
    {
    case ENCA_EVENT_NONE:
      return "none";
    case ENCA_EVENT_BUFFER_CHANGED:
      return "buffer-changed";
    case ENCA_EVENT_BUFFER_CREATED:
      return "buffer-created";
    case ENCA_EVENT_BUFFER_DELETED:
      return "buffer-deleted";
    case ENCA_EVENT_CURSOR_MOVED:
      return "cursor-moved";
    case ENCA_EVENT_WINDOW_CHANGED:
      return "window-changed";
    case ENCA_EVENT_TIMER:
      return "timer";
    case ENCA_EVENT_PROCESS:
      return "process";
    case ENCA_EVENT_RUNTIME:
      return "runtime";
    }
  return "unknown";
}
