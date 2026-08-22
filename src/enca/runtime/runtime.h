#ifndef ENCA_RUNTIME_H
#define ENCA_RUNTIME_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../event/event.h"
#include "../queue/blocking_queue.h"
#include "../thread/thread.h"
#include "../cancel/cancel.h"
#include "../trace/profiler.h"

#include <stdatomic.h>

#define ENCA_RT_MAX_WORKERS 32
#define ENCA_RT_WORKER_POLL_MS 50

#define ENCA_EVFLAG_TASK_SUBMIT ((enca_flags_t) (1u << 0))
#define ENCA_EVFLAG_TASK_RESULT ((enca_flags_t) (1u << 1))

typedef struct enca_task_input
{
  enca_object_id source_id;
  enca_u64 task_seq;
  enca_u64 revision;
  enca_flags_t flags;
  enca_usize input_size;
  unsigned char *input_data;
} enca_task_input;

typedef struct enca_task_result
{
  enca_object_id source_id;
  enca_u64 task_seq;
  enca_u64 revision;
  enca_u64 value;
  enca_timestamp_ns submit_ns;
  enca_timestamp_ns complete_ns;
} enca_task_result;

typedef void (*enca_rt_commit_fn) (const enca_task_result *result,
                                   void *ctx);

typedef struct enca_runtime
{
  enca_blocking_queue tasks;

  enca_mutex res_lock;
  enca_condition res_pop_cond;
  bool res_closed;
  enca_usize res_count;
  struct enca_rt_res_node *res_head;
  struct enca_rt_res_node *res_tail;

  enca_thread workers[ENCA_RT_MAX_WORKERS];
  unsigned worker_count;

  _Atomic enca_u64 current_generation;
  _Atomic enca_u64 next_task_seq;
  _Atomic (enca_cancel_source *) gen_cancel;
  _Atomic bool running;

  enca_mutex state_lock;

  enca_histogram h_submit_ns;
  enca_histogram h_complete_latency_ns;
  enca_histogram h_poll_block_ns;

  enca_counter tasks_submitted;
  enca_counter tasks_completed_by_worker;
  enca_counter tasks_cancelled_cooperative;
  enca_counter results_committed;
  enca_counter results_dropped_stale;
  enca_counter results_discarded_shutdown;
} enca_runtime;

enca_result enca_runtime_init (enca_runtime *rt, unsigned worker_count,
                               enca_u32 queue_capacity);
void enca_runtime_destroy (enca_runtime *rt);

ENCA_NODISCARD enca_result enca_runtime_submit (enca_runtime *rt,
                                                enca_object_id source_id,
                                                enca_flags_t flags,
                                                const void *data,
                                                enca_usize n);

enca_u64 enca_runtime_current_generation (const enca_runtime *rt);
void enca_runtime_advance_generation (enca_runtime *rt);

enca_usize enca_runtime_poll_results (enca_runtime *rt,
                                      enca_usize max_results,
                                      enca_rt_commit_fn commit_cb,
                                      void *cb_ctx);

enca_result enca_runtime_shutdown (enca_runtime *rt, enca_deadline deadline);

ENCA_INLINE enca_u64
enca_runtime_tasks_submitted (const enca_runtime *rt)
{
  return enca_counter_get (&rt->tasks_submitted);
}

ENCA_INLINE enca_u64
enca_runtime_results_committed (const enca_runtime *rt)
{
  return enca_counter_get (&rt->results_committed);
}

ENCA_INLINE enca_u64
enca_runtime_results_dropped_stale (const enca_runtime *rt)
{
  return enca_counter_get (&rt->results_dropped_stale);
}

ENCA_INLINE const enca_histogram *
enca_runtime_poll_histogram (const enca_runtime *rt)
{
  return &rt->h_poll_block_ns;
}

#endif
