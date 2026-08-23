/* enca-emacs.c --- minimal embedding of the ENCA runtime foundation.

Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* Minimal embedding (P1.11): prove that ENCA can live inside the
   Emacs process as a long-running runtime without changing any
   user-visible behavior.

   Invariants (src/enca/ARCHITECTURE.md):
   - Worker threads operate on native bytes only; they never touch a
     Lisp_Object and never call Elisp (#1-#3).
   - Results are committed on the main thread at Elisp-safe points,
     i.e. where running arbitrary Lisp is as legal as running timers.
   - Shutdown joins all workers before destruction (#8).
   - Nothing activates by itself: the runtime starts lazily on the
     first `enca-submit' call, so stock sessions are unaffected (#9).  */

#include <config.h>

#ifdef HAVE_ENCA

#include "lisp.h"

#include "enca/runtime/runtime.h"

/* Sizing for the minimal embedding.  Kept conservative: workers are
   pure CPU hashers at this stage.  */
#define ENCA_GLUE_WORKERS 2
#define ENCA_GLUE_QUEUE_CAPACITY 1024
#define ENCA_GLUE_SHUTDOWN_MS 2000
#define ENCA_GLUE_POLL_BATCH 64

/* Fixed native source id for tasks submitted through this glue.  A
   registry of real sources arrives with Phase 2 object ownership.  */
#define ENCA_GLUE_SOURCE_ID ((enca_object_id) 1)

static enca_runtime enca_glue_rt;
static bool enca_glue_running;

/* Result handler invoked from pump points on the main thread.
   staticpro'd so GC can see it across waits.  */
static Lisp_Object enca_glue_handler;

static bool
enca_glue_start (void)
{
  if (enca_glue_running)
    return true;
  if (enca_runtime_init (&enca_glue_rt, ENCA_GLUE_WORKERS,
			 ENCA_GLUE_QUEUE_CAPACITY) != ENCA_OK)
    return false;
  enca_glue_running = true;
  return true;
}

static void
enca_glue_stop (void)
{
  if (!enca_glue_running)
    return;
  enca_runtime_shutdown (&enca_glue_rt,
			 enca_deadline_from_now_ms (ENCA_GLUE_SHUTDOWN_MS));
  enca_runtime_destroy (&enca_glue_rt);
  enca_glue_running = false;
}

/* Commit one completed result by calling the handler as
   (funcall HANDLER SOURCE-ID SEQ GENERATION VALUE).  Runs on the main
   thread at an Elisp-safe point; may run GC and may quit.  */
static void
enca_glue_commit (const enca_task_result *result, void *ctx)
{
  Lisp_Object handler = *(Lisp_Object *) ctx;

  calln (handler,
	 make_uint (result->source_id),
	 make_uint (result->task_seq),
	 make_uint (result->revision),
	 make_uint (result->value));
}

DEFUN ("enca-available-p", Fenca_available_p, Senca_available_p, 0, 0, 0,
       doc: /* Return t when the ENCA runtime foundation is linked in.  */)
  (void)
{
  return Qt;
}

DEFUN ("enca-submit", Fenca_submit, Senca_submit, 1, 1, 0,
       doc: /* Submit STRING's bytes to the ENCA runtime as one task.
Starts the runtime lazily if needed.  Workers compute a checksum of
the bytes off-thread using native data only.  Returns the current
generation as an integer.  Results are delivered later by `enca-poll'
or by automatic pumping while Emacs waits.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  if (!enca_glue_start ())
    error ("ENCA runtime failed to start");

  ptrdiff_t size = SBYTES (string);
  enca_result r = enca_runtime_submit (&enca_glue_rt,
				       ENCA_GLUE_SOURCE_ID, 0,
				       SDATA (string), (enca_usize) size);
  if (ENCA_RESULT_IS_ERR (r))
    error ("ENCA submit failed: %s", enca_result_str (r));

  return make_uint (enca_runtime_current_generation (&enca_glue_rt));
}

DEFUN ("enca-set-handler", Fenca_set_handler, Senca_set_handler, 1, 1, 0,
       doc: /* Set FUNCTION as the ENCA result handler.
It is called as (funcall FUNCTION SOURCE-ID SEQ GENERATION VALUE)
whenever completed results are pumped on the main thread.  */)
  (Lisp_Object function)
{
  if (!FUNCTIONP (function))
    wrong_type_argument (Qfunctionp, function);
  enca_glue_handler = function;
  return function;
}

DEFUN ("enca-poll", Fenca_poll, Senca_poll, 0, 1, 0,
       doc: /* Drain completed ENCA results without blocking.
Each current-generation result is passed to the handler set by
`enca-set-handler'.  Optional MAX limits how many results to commit
this call.  Returns the number of results committed.  */)
  (Lisp_Object max)
{
  EMACS_INT limit = ENCA_GLUE_POLL_BATCH;

  if (!NILP (max))
    {
      CHECK_FIXNAT (max);
      limit = XFIXNAT (max);
    }

  if (!enca_glue_running || NILP (enca_glue_handler))
    return make_fixnum (0);

  enca_usize n = enca_runtime_poll_results (&enca_glue_rt,
					    (enca_usize) limit,
					    enca_glue_commit,
					    &enca_glue_handler);
  return make_fixnum (n);
}

DEFUN ("enca-cancel", Fenca_cancel, Senca_cancel, 0, 0, 0,
       doc: /* Advance the ENCA generation, cancelling in-flight work.
Results computed for older generations become stale and are dropped
instead of being committed.  Returns the new generation.  */)
  (void)
{
  if (!enca_glue_running)
    return make_uint (0);

  enca_runtime_advance_generation (&enca_glue_rt);
  return make_uint (enca_runtime_current_generation (&enca_glue_rt));
}

DEFUN ("enca-status", Fenca_status, Senca_status, 0, 0, 0,
       doc: /* Return ENCA runtime counters as a vector of integers.
Slots: GENERATION SUBMITTED COMPLETED COMMITTED STALE CANCELLED
DISCARDED.  Counters read zero while the runtime is not running.  */)
  (void)
{
  enca_u64 v[7] = { 0 };

  if (enca_glue_running)
    {
      v[0] = enca_runtime_current_generation (&enca_glue_rt);
      v[1] = enca_counter_get (&enca_glue_rt.tasks_submitted);
      v[2] = enca_counter_get (&enca_glue_rt.tasks_completed_by_worker);
      v[3] = enca_counter_get (&enca_glue_rt.results_committed);
      v[4] = enca_counter_get (&enca_glue_rt.results_dropped_stale);
      v[5] = enca_counter_get (&enca_glue_rt.tasks_cancelled_cooperative);
      v[6] = enca_counter_get (&enca_glue_rt.results_discarded_shutdown);
    }

  Lisp_Object vec = make_vector ((ptrdiff_t) countof (v), Qnil);
  for (int i = 0; i < (int) countof (v); i++)
    ASET (vec, i, make_uint (v[i]));
  return vec;
}

DEFUN ("enca-shutdown", Fenca_shutdown, Senca_shutdown, 0, 0, 0,
       doc: /* Stop the ENCA runtime and join all worker threads.
Pending uncommitted results are discarded.  A later `enca-submit'
restarts the runtime.  */)
  (void)
{
  enca_glue_stop ();
  return Qt;
}

/* Pump completed results into the handler.  Called from main-thread
   wait points that already allow running arbitrary Lisp (the same
   context in which timers run), so committing here is safe.  Must
   stay cheap when idle: two atomic/mutex probes and out.  */
void
enca_glue_pump (void)
{
  if (!enca_glue_running || NILP (enca_glue_handler))
    return;

  enca_runtime_poll_results (&enca_glue_rt, ENCA_GLUE_POLL_BATCH,
			     enca_glue_commit, &enca_glue_handler);
}

/* Join workers during normal shutdown (sig == 0 only; after fatal
   signals the process is going down regardless).  Never signals:
   Vrun_hooks is already Qnil and Elisp cannot run here.  */
void
enca_glue_shutdown (void)
{
  enca_glue_stop ();
}

void
syms_of_enca (void)
{
  enca_glue_handler = Qnil;
  staticpro (&enca_glue_handler);

  defsubr (&Senca_available_p);
  defsubr (&Senca_submit);
  defsubr (&Senca_set_handler);
  defsubr (&Senca_poll);
  defsubr (&Senca_cancel);
  defsubr (&Senca_status);
  defsubr (&Senca_shutdown);

#ifdef HAVE_ENCA_EVS
  void syms_of_enca_evs (void);
  syms_of_enca_evs ();
#endif
}

#endif /* HAVE_ENCA */
