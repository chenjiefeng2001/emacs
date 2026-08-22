#include "test_util.h"
#include "../../src/enca/base/types.h"
#include "../../src/enca/diagnostics/error.h"

#include <setjmp.h>

int enca_test_failures;
int enca_test_checks;

static jmp_buf *active_panic_jmp;

void
enca_test_set_panic_jmp (jmp_buf *jb)
{
  active_panic_jmp = jb;
}

static void
test_panic_handler (const char *file, int line, const char *msg, void *ud)
{
  (void) file;
  (void) line;
  (void) msg;
  (void) ud;

  if (active_panic_jmp)
    longjmp (*active_panic_jmp, 1);
}

void
enca_test_run_suite (const char *name, enca_test_fn fn)
{
  int before_fails = enca_test_failures;
  int before_checks = enca_test_checks;

  printf ("[%s]\n", name);
  fflush (stdout);
  fn ();

  int fails = enca_test_failures - before_fails;
  int checks = enca_test_checks - before_checks;
  printf ("  %s (%d checks)\n", fails == 0 ? "ok" : "FAILED", checks);
  fflush (stdout);
}

int
main (void)
{
  enca_set_panic_handler (test_panic_handler, NULL);

  void run_test_base (void);
  void run_test_id (void);
  void run_test_memory (void);
  void run_test_arena (void);
  void run_test_slab (void);
  void run_test_time (void);
  void run_test_thread (void);
  void run_test_queue (void);
  void run_test_event (void);
  void run_test_cancel (void);
  void run_test_trace_perf (void);
  void run_test_runtime (void);

  run_test_base ();
  run_test_id ();
  run_test_memory ();
  run_test_arena ();
  run_test_slab ();
  run_test_time ();
  run_test_thread ();
  run_test_queue ();
  run_test_event ();
  run_test_cancel ();
  run_test_trace_perf ();
  run_test_runtime ();

  printf ("\n%d checks, %d failures\n", enca_test_checks,
          enca_test_failures);

  return enca_test_failures == 0 ? 0 : 1;
}
