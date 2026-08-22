ifeq ($(origin CC),default)
CC = clang
endif
CFLAGS ?= -std=c23 -O1 -g -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS
INCLUDES = -I../../src

ENCA_SRC = \
	../../src/enca/diagnostics/error.c \
	../../src/enca/id/id.c \
	../../src/enca/memory/allocator.c \
	../../src/enca/memory/arena.c \
	../../src/enca/memory/slab.c \
	../../src/enca/time/clock.c \
	../../src/enca/thread/thread.c \
	../../src/enca/event/event.c \
	../../src/enca/event/dispatch.c \
	../../src/enca/queue/spsc_ring.c \
	../../src/enca/queue/blocking_queue.c \
	../../src/enca/cancel/cancel.c \
	../../src/enca/trace/trace.c \
	../../src/enca/trace/profiler.c \
	../../src/enca/runtime/runtime.c \
	../../src/enca/snapshot/snapshot.c \
	../../src/enca/scheduler/scheduler.c

TEST_SRC = \
	main.c \
	test_base.c \
	test_id.c \
	test_memory.c \
	test_arena.c \
	test_slab.c \
	test_time.c \
	test_thread.c \
	test_queue.c \
	test_event.c \
	test_cancel.c \
	test_cancel_race.c \
	test_trace_perf.c \
	test_runtime.c \
	test_snapshot.c \
	test_scheduler.c

all: enca_tests

enca_tests: $(ENCA_SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(ENCA_SRC) $(TEST_SRC)

run: enca_tests
	./enca_tests

asan:
	$(CC) -std=c23 -O1 -g -fsanitize=address -fno-omit-frame-pointer \
		-Wall -Wextra $(INCLUDES) \
		-o enca_tests_asan $(ENCA_SRC) $(TEST_SRC)
	./enca_tests_asan

ubsan:
	$(CC) -std=c23 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -DENCA_TEST_UBSAN=1 \
		-Wall -Wextra $(INCLUDES) \
		-o enca_tests_ubsan $(ENCA_SRC) $(TEST_SRC)
	./enca_tests_ubsan

tsan:
	$(CC) -std=c23 -O1 -g -fsanitize=thread -fno-omit-frame-pointer \
		-Wall -Wextra $(INCLUDES) \
		-o enca_tests_tsan $(ENCA_SRC) $(TEST_SRC)
	./enca_tests_tsan

gcc-check:
	gcc -std=gnu11 -O1 -Wall -Wextra -D_WIN32_WINNT=0x0601 \
		-D_CRT_SECURE_NO_WARNINGS $(INCLUDES) \
		-o enca_tests_gcc $(ENCA_SRC) $(TEST_SRC)
	./enca_tests_gcc

clean:
	rm -f enca_tests enca_tests_asan enca_tests_gcc trace-dump-test.json

.PHONY: all run asan ubsan tsan gcc-check clean
