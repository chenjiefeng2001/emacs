#ifndef ENCA_EVENT_H
#define ENCA_EVENT_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

typedef enum enca_event_type
{
  ENCA_EVENT_NONE = 0,
  ENCA_EVENT_BUFFER_CHANGED = 1,
  ENCA_EVENT_BUFFER_CREATED = 2,
  ENCA_EVENT_BUFFER_DELETED = 3,
  ENCA_EVENT_CURSOR_MOVED = 4,
  ENCA_EVENT_WINDOW_CHANGED = 5,
  ENCA_EVENT_TIMER = 6,
  ENCA_EVENT_PROCESS = 7,
  ENCA_EVENT_RUNTIME = 8,
} enca_event_type;

typedef struct enca_event
{
  enca_event_type type;
  enca_object_id source;
  enca_seq_t sequence;
  enca_timestamp_ns timestamp;
  enca_flags_t flags;
  void *payload;
} enca_event;

typedef enca_result (*enca_event_handler) (const enca_event *e, void *ctx);

enca_seq_t enca_event_next_sequence (void);

void enca_event_init (enca_event *e, enca_event_type type,
                      enca_object_id source, enca_flags_t flags, void *payload);

const char *enca_event_type_str (enca_event_type type);

#endif
