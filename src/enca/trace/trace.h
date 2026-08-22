#ifndef ENCA_TRACE_H
#define ENCA_TRACE_H

#include "../base/types.h"
#include "../base/attributes.h"

#include <stdio.h>

typedef enum enca_trace_kind
{
  ENCA_TRACE_EVENT_PUBLISH = 0,
  ENCA_TRACE_EVENT_DISPATCH = 1,
  ENCA_TRACE_TASK_BEGIN = 2,
  ENCA_TRACE_TASK_END = 3,
  ENCA_TRACE_QUEUE_PUSH = 4,
  ENCA_TRACE_QUEUE_POP = 5,
  ENCA_TRACE_ALLOC = 6,
  ENCA_TRACE_FREE = 7,
  ENCA_TRACE_CUSTOM = 8,
} enca_trace_kind;

#define ENCA_TRACE_CAPACITY 65536

typedef struct enca_trace_record
{
  enca_timestamp_ns ts_ns;
  enca_u64 id;
  enca_u64 data;
  enca_u32 tid;
  enca_trace_kind kind;
} enca_trace_record;

void enca_trace_set_enabled (bool enabled);
bool enca_trace_enabled (void);
enca_usize enca_trace_record_count (void);

void enca_trace_emit (enca_trace_kind kind, enca_u64 id, enca_u64 data);

ENCA_NODISCARD bool enca_trace_dump_chrome (FILE *out);

const char *enca_trace_kind_str (enca_trace_kind kind);

#endif
