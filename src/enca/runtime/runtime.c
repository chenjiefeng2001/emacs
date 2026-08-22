#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "runtime.h"

#include "../base/assert.h"
#include "../memory/memory.h"
#include <stdio.h>
#include <string.h>

typedef struct enca_rt_res_node enca_rt_res_node;

struct enca_rt_res_node
{
  enca_rt_res_node *next;
  enca_task_result *payload;
};

static void
free_task_result_msg (enca_task_result *tr)
{
  enca_free (tr);
}

static void
res_push (enca_runtime *rt, enca_task_result *tr)
{
  enca_rt_res_node *n = enca_malloc (sizeof *n);
  if (!n)
    {
      free_task_result_msg (tr);
      return;
    }
  n->payload = tr;
  n->next = NULL;

  enca_mutex_lock (&rt->res_lock);
  if (rt->res_tail)
    rt->res_tail->next = n;
  else
    rt->res_head = n;
  rt->res_tail = n;
  rt->res_count++;
  enca_mutex_unlock (&rt->res_lock);
  enca_condition_signal (&rt->res_pop_cond);
}

static bool
res_try_pop_locked (enca_runtime *rt, enca_task_result **out)
{
  enca_rt_res_node *n = rt->res_head;

  if (!n)
    return false;

  *out = n->payload;
  rt->res_head = n->next;
  if (!rt->res_head)
    rt->res_tail = NULL;
  rt->res_count--;
  enca_free (n);
  return true;
}

static bool
res_try_pop (enca_runtime *rt, enca_task_result **out)
{
  enca_mutex_lock (&rt->res_lock);
  bool got = res_try_pop_locked (rt, out);
  enca_mutex_unlock (&rt->res_lock);
  return got;
}

static bool
res_pop_timed (enca_runtime *rt, enca_task_result **out, enca_u64 ns)
{
  enca_mutex_lock (&rt->res_lock);

  while (!rt->res_head && !rt->res_closed)
    {
      if (ns == 0 || !enca_condition_timed_wait (&rt->res_pop_cond,
                                                 &rt->res_lock, ns))
        break;
    }

  bool got = res_try_pop_locked (rt, out);
  enca_mutex_unlock (&rt->res_lock);
  return got;
}

static void
res_close (enca_runtime *rt)
{
  enca_mutex_lock (&rt->res_lock);
  rt->res_closed = true;
  enca_mutex_unlock (&rt->res_lock);
  enca_condition_broadcast (&rt->res_pop_cond);
}

static void
res_discard_all (enca_runtime *rt)
{
  enca_task_result *tr;

  enca_mutex_lock (&rt->res_lock);
  while (rt->res_head)
    {
      enca_rt_res_node *n = rt->res_head;
      rt->res_head = n->next;
      tr = n->payload;
      enca_free (n);
      free_task_result_msg (tr);
      enca_counter_add (&rt->results_discarded_shutdown, 1);
    }
  rt->res_tail = NULL;
  rt->res_count = 0;
  enca_mutex_unlock (&rt->res_lock);
}

static enca_u64
fnv1a_chunk (enca_u64 hash, const unsigned char *p, enca_usize n)
{
  for (enca_usize i = 0; i < n; i++)
    {
      hash ^= p[i];
      hash *= (enca_u64) 1099511628211ull;
    }
  return hash;
}

static void
free_task_input (enca_task_input *ti)
{
  enca_task_input_destroy (ti);
}

void
enca_task_input_destroy (enca_task_input *ti)
{
  if (!ti)
    return;
  if (ti->input_destroy)
    ti->input_destroy (ti);
  else
    enca_free (ti->input_data);
  enca_free (ti);
}

static bool
generation_cancelled (enca_runtime *rt, enca_u64 revision)
{
  if (atomic_load_explicit (&rt->current_generation,
                            memory_order_acquire) != revision)
    return true;

  enca_mutex_lock (&rt->state_lock);
  enca_cancel_source *src = atomic_load_explicit (&rt->gen_cancel,
                                                  memory_order_acquire);
  enca_cancel_source_retain (src);
  enca_mutex_unlock (&rt->state_lock);

  bool cancelled = enca_cancel_source_is_cancelled (src);
  enca_cancel_source_release (src);
  return cancelled;
}

static enca_result
worker_main (void *arg)
{
  enca_runtime *rt = arg;
  enca_event ev;
  enca_deadline poll_dl = enca_deadline_from_now_ms (ENCA_RT_WORKER_POLL_MS);

  for (;;)
    {
      enca_result r = enca_bq_pop (&rt->tasks, &ev, poll_dl);

      if (r == ENCA_ERR_TIMEOUT)
        {
          poll_dl = enca_deadline_from_now_ms (ENCA_RT_WORKER_POLL_MS);
          continue;
        }
      if (ENCA_RESULT_IS_ERR (r))
        break;

      enca_task_input *ti = ev.payload;
      ENCA_ASSERT_ALWAYS (ti != NULL, "null task payload");

      enca_task_result *tr = enca_malloc (sizeof *tr);
      if (!tr)
        {
          free_task_input (ti);
          continue;
        }

      tr->source_id = ti->source_id;
      tr->task_seq = ti->task_seq;
      tr->revision = ti->revision;
      tr->stream_revision = ti->stream_revision;
      tr->submit_ns = ev.timestamp;
      tr->value = 0;

      const unsigned char *p = ti->input_data;
      enca_usize left = ti->input_size;
      enca_u64 hash = (enca_u64) 1469598103934665603ull;
      bool cancelled = false;

      while (left > 0)
        {
          enca_usize chunk = left > 4096 ? 4096 : left;
          hash = fnv1a_chunk (hash, p, chunk);
          p += chunk;
          left -= chunk;

          if (generation_cancelled (rt, ti->revision))
            {
              cancelled = true;
              break;
            }
        }

      if (cancelled)
        {
          enca_counter_add (&rt->tasks_cancelled_cooperative, 1);
          free_task_input (ti);
          free_task_result_msg (tr);
          continue;
        }

      tr->value = hash;
      tr->complete_ns = enca_monotonic_now_ns ();

      res_push (rt, tr);

      /* Counted only once the result is observable in the queue. */
      enca_counter_add (&rt->tasks_completed_by_worker, 1);

      free_task_input (ti);
    }

  return ENCA_OK;
}

enca_result
enca_runtime_init (enca_runtime *rt, unsigned worker_count,
                   enca_u32 queue_capacity)
{
  if (!rt || worker_count == 0 || worker_count > ENCA_RT_MAX_WORKERS)
    return ENCA_ERR_INVALID_ARGUMENT;

  memset (rt, 0, sizeof *rt);

  enca_result r = enca_mutex_init (&rt->state_lock);
  if (ENCA_RESULT_IS_ERR (r))
    return r;

  r = enca_bq_init (&rt->tasks, queue_capacity);
  if (ENCA_RESULT_IS_ERR (r))
    goto fail_tasks;

  r = enca_mutex_init (&rt->res_lock);
  if (ENCA_RESULT_IS_ERR (r))
    goto fail_reslock;

  r = enca_condition_init (&rt->res_pop_cond);
  if (ENCA_RESULT_IS_ERR (r))
    goto fail_rescond;

  rt->res_closed = false;
  rt->res_count = 0;
  rt->res_head = NULL;
  rt->res_tail = NULL;

  enca_cancel_source *src = NULL;
  r = enca_cancel_source_create (&src);
  if (ENCA_RESULT_IS_ERR (r))
    {
      enca_condition_destroy (&rt->res_pop_cond);
      enca_mutex_destroy (&rt->res_lock);
      enca_bq_destroy (&rt->tasks);
      enca_mutex_destroy (&rt->state_lock);
      memset (rt, 0, sizeof *rt);
      return r;
    }

  atomic_store_explicit (&rt->gen_cancel, src, memory_order_release);
  atomic_store_explicit (&rt->current_generation, 1, memory_order_relaxed);
  atomic_store_explicit (&rt->next_task_seq, 1, memory_order_relaxed);
  atomic_store_explicit (&rt->running, true, memory_order_release);

  enca_histogram_init (&rt->h_submit_ns);
  enca_histogram_init (&rt->h_complete_latency_ns);
  enca_histogram_init (&rt->h_poll_block_ns);
  enca_counter_init (&rt->tasks_submitted);
  enca_counter_init (&rt->tasks_completed_by_worker);
  enca_counter_init (&rt->tasks_cancelled_cooperative);
  enca_counter_init (&rt->results_committed);
  enca_counter_init (&rt->results_dropped_stale);
  enca_counter_init (&rt->results_discarded_shutdown);

  rt->worker_count = worker_count;
  for (unsigned i = 0; i < worker_count; i++)
    {
      char name[32];
      snprintf (name, sizeof name, "enca-worker-%u", i);
      r = enca_thread_create (&rt->workers[i], name, worker_main, rt);
      if (ENCA_RESULT_IS_ERR (r))
        {
          rt->worker_count = i;
          enca_runtime_shutdown (rt, enca_deadline_from_now_ms (2000));
          return r;
        }
    }

  return ENCA_OK;

fail_rescond:
  enca_mutex_destroy (&rt->res_lock);
fail_reslock:
  enca_bq_destroy (&rt->tasks);
fail_tasks:
  enca_mutex_destroy (&rt->state_lock);
  memset (rt, 0, sizeof *rt);
  return r;
}

void
enca_runtime_destroy (enca_runtime *rt)
{
  if (!rt)
    return;

  enca_cancel_source *src = atomic_exchange_explicit (&rt->gen_cancel, NULL,
                                                      memory_order_acq_rel);
  enca_cancel_source_release (src);

  res_discard_all (rt);
  enca_condition_destroy (&rt->res_pop_cond);
  enca_mutex_destroy (&rt->res_lock);
  enca_bq_destroy (&rt->tasks);
  enca_mutex_destroy (&rt->state_lock);
  memset (rt, 0, sizeof *rt);
}

enca_result
enca_runtime_submit (enca_runtime *rt, enca_object_id source_id,
                     enca_flags_t flags, const void *data, enca_usize n)
{
  enca_task_submit req;

  req.source_id = source_id;
  req.flags = flags;
  req.data = data;
  req.n = n;
  req.stream_revision = 0;
  req.user_data = NULL;
  req.input_destroy = NULL;
  return enca_runtime_submit_ex (rt, &req);
}

enca_result
enca_runtime_submit_ex (enca_runtime *rt, const enca_task_submit *req)
{
  if (!rt || !req || (!req->data && req->n > 0))
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_timer t;
  enca_timer_begin (&t);

  enca_u64 gen = atomic_load_explicit (&rt->current_generation,
                                       memory_order_acquire);

  enca_task_input *ti = enca_malloc (sizeof *ti);
  if (!ti)
    return ENCA_ERR_OUT_OF_MEMORY;

  ti->input_data = NULL;
  if (req->n > 0)
    {
      if (req->flags & ENCA_TASK_BORROW_INPUT)
        {
          /* Borrowed view: lifetime guaranteed by input_destroy. */
          ti->input_data = (unsigned char *) req->data;
        }
      else
        {
          ti->input_data = enca_malloc (req->n);
          if (!ti->input_data)
            {
              enca_free (ti);
              return ENCA_ERR_OUT_OF_MEMORY;
            }
          memcpy (ti->input_data, req->data, req->n);
        }
    }

  ti->source_id = req->source_id;
  ti->revision = gen;
  ti->stream_revision = req->stream_revision;
  ti->flags = req->flags;
  ti->input_size = req->n;
  ti->user_data = req->user_data;
  ti->input_destroy = req->input_destroy;
  ti->task_seq = atomic_fetch_add_explicit (&rt->next_task_seq, 1,
                                            memory_order_relaxed);

  enca_event ev;
  ev.type = ENCA_EVENT_RUNTIME;
  ev.flags = ENCA_EVFLAG_TASK_SUBMIT;
  ev.source = ti->source_id;
  ev.sequence = ti->task_seq;
  ev.timestamp = enca_monotonic_now_ns ();
  ev.payload = ti;

  enca_deadline dl = enca_deadline_from_now_ms (5000);
  enca_result r = enca_bq_push (&rt->tasks, &ev, dl);
  if (ENCA_RESULT_IS_ERR (r))
    {
      free_task_input (ti);
      return r;
    }

  enca_counter_add (&rt->tasks_submitted, 1);
  enca_histogram_record_ns (&rt->h_submit_ns, enca_timer_end_ns (&t));
  return ENCA_OK;
}

enca_u64
enca_runtime_current_generation (const enca_runtime *rt)
{
  return rt ? atomic_load_explicit (
    (const _Atomic enca_u64 *) &rt->current_generation,
    memory_order_acquire)
            : 0;
}

void
enca_runtime_advance_generation (enca_runtime *rt)
{
  enca_mutex_lock (&rt->state_lock);

  enca_cancel_source *old = atomic_exchange_explicit (
    &rt->gen_cancel, NULL, memory_order_acq_rel);
  if (old)
    {
      enca_cancel_source_cancel (old);
      enca_cancel_source_release (old);
    }

  enca_cancel_source *fresh = NULL;
  if (enca_cancel_source_create (&fresh) == ENCA_OK)
    atomic_store_explicit (&rt->gen_cancel, fresh, memory_order_release);

  atomic_fetch_add_explicit (&rt->current_generation, 1,
                             memory_order_acq_rel);

  enca_mutex_unlock (&rt->state_lock);
}

enca_usize
enca_runtime_poll_results (enca_runtime *rt, enca_usize max_results,
                           enca_rt_commit_fn commit_cb, void *cb_ctx)
{
  if (!rt)
    return 0;

  enca_timer t;
  enca_timer_begin (&t);

  enca_u64 cur_gen = atomic_load_explicit (&rt->current_generation,
                                           memory_order_acquire);
  enca_usize processed = 0;

  while (processed < max_results)
    {
      enca_task_result *tr = NULL;

      if (!res_try_pop (rt, &tr))
        break;

      if (tr->revision == cur_gen)
        {
          enca_counter_add (&rt->results_committed, 1);
          if (commit_cb)
            commit_cb (tr, cb_ctx);
        }
      else
        {
          enca_counter_add (&rt->results_dropped_stale, 1);
        }

      enca_histogram_record_ns (&rt->h_complete_latency_ns,
                                tr->complete_ns - tr->submit_ns);
      free_task_result_msg (tr);
      processed++;
    }

  enca_u64 block = enca_timer_end_ns (&t);
  if (block > 0)
    enca_histogram_record_ns (&rt->h_poll_block_ns, block);

  return processed;
}

enca_result
enca_runtime_shutdown (enca_runtime *rt, enca_deadline deadline)
{
  if (!rt)
    return ENCA_ERR_INVALID_ARGUMENT;

  bool expected = true;
  if (!atomic_compare_exchange_strong_explicit (
        &rt->running, &expected, false, memory_order_acq_rel,
        memory_order_acquire))
    return ENCA_ERR_INTERNAL;

  enca_bq_close (&rt->tasks);
  res_close (rt);

  for (unsigned i = 0; i < rt->worker_count; i++)
    enca_thread_join (&rt->workers[i]);
  rt->worker_count = 0;

  {
    enca_u64 remain_ns = enca_deadline_remaining_ns (deadline);

    while (remain_ns > 0)
      {
        enca_task_result *tr = NULL;

        if (!res_pop_timed (rt, &tr, remain_ns))
          break;

        free_task_result_msg (tr);
        enca_counter_add (&rt->results_discarded_shutdown, 1);
        remain_ns = enca_deadline_remaining_ns (deadline);
      }

    res_discard_all (rt);
  }

  for (;;)
    {
      enca_event ev;
      enca_result r = enca_bq_try_pop (&rt->tasks, &ev);
      if (ENCA_RESULT_IS_ERR (r))
        break;
      free_task_input (ev.payload);
    }

  return ENCA_OK;
}
