#!/bin/bash
# EVS-3: TSan run over the full native suite (Linux/WSL gcc).
set -e
cd /mnt/c/Users/14977/source/repos/emacs/test/enca
SRC="../../src/enca"
gcc -std=gnu2x -O1 -g -fsanitize=thread -fno-omit-frame-pointer \
    -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -I../../src \
    -o /tmp/enca_tsan "$SRC/diagnostics/error.c" "$SRC/id/id.c" \
    "$SRC/memory/allocator.c" "$SRC/memory/arena.c" "$SRC/memory/slab.c" \
    "$SRC/time/clock.c" "$SRC/thread/thread.c" "$SRC/event/event.c" \
    "$SRC/event/dispatch.c" "$SRC/queue/spsc_ring.c" "$SRC/queue/blocking_queue.c" \
    "$SRC/cancel/cancel.c" "$SRC/trace/trace.c" "$SRC/trace/profiler.c" \
    "$SRC/runtime/runtime.c" "$SRC/snapshot/snapshot.c" \
    "$SRC/scheduler/scheduler.c" "$SRC/wake/wake.c" \
    "$SRC/completion/completion.c" \
    main.c test_base.c test_id.c test_memory.c test_arena.c test_slab.c \
    test_time.c test_thread.c test_queue.c test_event.c test_cancel.c \
    test_cancel_race.c test_trace_perf.c test_runtime.c test_snapshot.c \
    test_scheduler.c test_wake.c test_completion.c test_dstate.c
TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1" /tmp/enca_tsan > /tmp/tsan_out.txt 2>&1
rc=$?
echo "RUN_RC=$rc"
tail -3 /tmp/tsan_out.txt
echo "WARNINGS=$(grep -c 'WARNING: ThreadSanitizer' /tmp/tsan_out.txt || true)"
grep -A12 'WARNING: ThreadSanitizer' /tmp/tsan_out.txt | head -40 || true
