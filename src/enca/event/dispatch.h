#ifndef ENCA_DISPATCH_H
#define ENCA_DISPATCH_H

#include "event.h"
#include "../base/types.h"
#include "../time/time.h"

struct enca_spsc_ring;
struct enca_blocking_queue;

enca_usize enca_event_dispatch_spsc (struct enca_spsc_ring *q,
                                     enca_event_handler handler, void *ctx,
                                     enca_usize max_events);

enca_result enca_event_dispatch_blocking (struct enca_blocking_queue *q,
                                          enca_event_handler handler,
                                          void *ctx, enca_deadline deadline);

#endif
