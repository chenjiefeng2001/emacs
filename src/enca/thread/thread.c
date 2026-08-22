#include "thread.h"

#include "../base/assert.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
# endif
# include <windows.h>
#else
# include <sched.h>
# include <time.h>
#endif

static _Atomic enca_u64 tls_slot_used[ENCA_TLS_MAX_SLOTS / 64];
#if !defined(_WIN32)
static _Atomic enca_u64 tls_next_id;
#endif

static ENCA_THREAD_LOCAL void *tls_values[ENCA_TLS_MAX_SLOTS];
#if !defined(_WIN32)
static ENCA_THREAD_LOCAL enca_u64 tls_thread_id;
#endif

enca_result
enca_tls_alloc (unsigned *out_slot)
{
  if (!out_slot)
    return ENCA_ERR_INVALID_ARGUMENT;

  for (unsigned word = 0; word < ENCA_TLS_MAX_SLOTS / 64; word++)
    {
      enca_u64 used = atomic_load_explicit (&tls_slot_used[word],
                                            memory_order_relaxed);
      if (used == UINT64_MAX)
        continue;

      for (unsigned bit = 0; bit < 64; bit++)
        {
          enca_u64 mask = (enca_u64) 1 << bit;
          if (used & mask)
            continue;

          enca_u64 desired = used | mask;
          if (!atomic_compare_exchange_strong (&tls_slot_used[word], &used,
                                               desired))
            {
              used = atomic_load_explicit (&tls_slot_used[word],
                                           memory_order_relaxed);
              bit = (unsigned) -1;
              continue;
            }
          *out_slot = word * 64 + bit;
          return ENCA_OK;
        }
    }
  return ENCA_ERR_CAPACITY;
}

enca_result
enca_tls_free (unsigned slot)
{
  if (slot >= ENCA_TLS_MAX_SLOTS)
    return ENCA_ERR_INVALID_ARGUMENT;

  unsigned word = slot / 64, bit = slot % 64;
  atomic_fetch_and_explicit (&tls_slot_used[word], ~((enca_u64) 1 << bit),
                             memory_order_relaxed);
  return ENCA_OK;
}

void *
enca_tls_get (unsigned slot)
{
  ENCA_ASSERT (slot < ENCA_TLS_MAX_SLOTS, "tls slot out of range");
  if (slot >= ENCA_TLS_MAX_SLOTS)
    return NULL;
  return tls_values[slot];
}

bool
enca_tls_set (unsigned slot, void *value)
{
  ENCA_ASSERT (slot < ENCA_TLS_MAX_SLOTS, "tls slot out of range");
  if (slot >= ENCA_TLS_MAX_SLOTS)
    return false;
  tls_values[slot] = value;
  return true;
}

enca_u32
enca_thread_self_id (void)
{
#if defined(_WIN32)
  return (enca_u32) GetCurrentThreadId ();
#else
  if (!tls_thread_id)
    tls_thread_id = atomic_fetch_add_explicit (&tls_next_id, 1,
                                               memory_order_relaxed)
                    + 1;
  return (enca_u32) tls_thread_id;
#endif
}

void
enca_thread_yield (void)
{
#if defined(_WIN32)
  SwitchToThread ();
#else
  sched_yield ();
#endif
}

#if defined(_WIN32)

# define WIN32_LEAN_AND_MEAN
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
# endif
# include <windows.h>

typedef struct thread_start_ctx
{
  enca_thread_fn fn;
  void *arg;
  wchar_t name[64];
} thread_start_ctx;

typedef HRESULT WINAPI set_thread_description_fn (HANDLE, PCWSTR);

static DWORD WINAPI
thread_trampoline (LPVOID param)
{
  thread_start_ctx ctx = *(thread_start_ctx *) param;
  free (param);

  HMODULE k32 = GetModuleHandleW (L"kernel32.dll");
  if (k32 && ctx.name[0])
    {
      set_thread_description_fn *set_desc
        = (set_thread_description_fn *) (void *) GetProcAddress (
          k32, "SetThreadDescription");
      if (set_desc)
        set_desc (GetCurrentThread (), ctx.name);
    }

  return (DWORD) ctx.fn (ctx.arg);
}

enca_result
enca_thread_create (enca_thread *t, const char *name, enca_thread_fn fn,
                    void *arg)
{
  if (!t || !fn)
    return ENCA_ERR_INVALID_ARGUMENT;

  thread_start_ctx *ctx = malloc (sizeof *ctx);
  if (!ctx)
    return ENCA_ERR_OUT_OF_MEMORY;
  ctx->fn = fn;
  ctx->arg = arg;
  ctx->name[0] = 0;
  if (name)
    {
      size_t i = 0;
      for (; i + 1 < sizeof ctx->name / sizeof ctx->name[0] && name[i]; i++)
        ctx->name[i] = (wchar_t) (unsigned char) name[i];
      ctx->name[i] = 0;
    }

  HANDLE h = CreateThread (NULL, 0, thread_trampoline, ctx, 0, NULL);
  if (!h)
    {
      free (ctx);
      return ENCA_ERR_INTERNAL;
    }

  t->native = h;
  t->id = 0;
  t->detached = false;
  return ENCA_OK;
}

enca_result
enca_thread_join (enca_thread *t)
{
  if (!t || !t->native)
    return ENCA_ERR_INVALID_ARGUMENT;
  if (t->detached)
    return ENCA_ERR_INTERNAL;

  WaitForSingleObject ((HANDLE) t->native, INFINITE);
  CloseHandle ((HANDLE) t->native);
  t->native = NULL;
  return ENCA_OK;
}

enca_result
enca_thread_detach (enca_thread *t)
{
  if (!t || !t->native)
    return ENCA_ERR_INVALID_ARGUMENT;

  CloseHandle ((HANDLE) t->native);
  t->native = NULL;
  t->detached = true;
  return ENCA_OK;
}

enca_result
enca_mutex_init (enca_mutex *m)
{
  if (!m)
    return ENCA_ERR_INVALID_ARGUMENT;
  InitializeCriticalSectionAndSpinCount ((LPCRITICAL_SECTION) (LPCRITICAL_SECTION) &m->native, 400);
  return ENCA_OK;
}

void
enca_mutex_destroy (enca_mutex *m)
{
  if (m)
    DeleteCriticalSection ((LPCRITICAL_SECTION) &m->native);
}

void
enca_mutex_lock (enca_mutex *m)
{
  EnterCriticalSection ((LPCRITICAL_SECTION) &m->native);
}

void
enca_mutex_unlock (enca_mutex *m)
{
  LeaveCriticalSection ((LPCRITICAL_SECTION) &m->native);
}

bool
enca_mutex_try_lock (enca_mutex *m)
{
  return TryEnterCriticalSection ((LPCRITICAL_SECTION) &m->native) != 0;
}

enca_result
enca_condition_init (enca_condition *c)
{
  if (!c)
    return ENCA_ERR_INVALID_ARGUMENT;
  InitializeConditionVariable ((PCONDITION_VARIABLE) &c->native);
  return ENCA_OK;
}

void
enca_condition_destroy (enca_condition *c)
{
  (void) c;
}

void
enca_condition_wait (enca_condition *c, enca_mutex *m)
{
  SleepConditionVariableCS ((PCONDITION_VARIABLE) &c->native, (LPCRITICAL_SECTION) &m->native,
                            INFINITE);
}

bool
enca_condition_timed_wait (enca_condition *c, enca_mutex *m, enca_u64 ns)
{
  DWORD ms;
  if (ns == UINT64_MAX || ns > (enca_u64) 0x7FFFFFFF * 1000000ull)
    ms = INFINITE;
  else
    ms = (DWORD) (ns / 1000000ull) + 1;
  return SleepConditionVariableCS ((PCONDITION_VARIABLE) &c->native,
                                   (LPCRITICAL_SECTION) &m->native, ms) != 0;
}

void
enca_condition_signal (enca_condition *c)
{
  WakeConditionVariable ((PCONDITION_VARIABLE) &c->native);
}

void
enca_condition_broadcast (enca_condition *c)
{
  WakeAllConditionVariable ((PCONDITION_VARIABLE) &c->native);
}

#else

# include <sched.h>
# include <time.h>
# include "../time/time.h"

typedef struct thread_start_ctx
{
  enca_thread_fn fn;
  void *arg;
} thread_start_ctx;

static void *
thread_trampoline (void *param)
{
  thread_start_ctx ctx = *(thread_start_ctx *) param;
  free (param);
  return (void *) (uintptr_t) ctx.fn (ctx.arg);
}

enca_result
enca_thread_create (enca_thread *t, const char *name, enca_thread_fn fn,
                    void *arg)
{
  (void) name;

  if (!t || !fn)
    return ENCA_ERR_INVALID_ARGUMENT;

  thread_start_ctx *ctx = malloc (sizeof *ctx);
  if (!ctx)
    return ENCA_ERR_OUT_OF_MEMORY;
  ctx->fn = fn;
  ctx->arg = arg;

  if (pthread_create (&t->native, NULL, thread_trampoline, ctx) != 0)
    {
      free (ctx);
      return ENCA_ERR_INTERNAL;
    }
  t->id = 0;
  t->detached = false;
  return ENCA_OK;
}

enca_result
enca_thread_join (enca_thread *t)
{
  if (!t)
    return ENCA_ERR_INVALID_ARGUMENT;
  if (t->detached)
    return ENCA_ERR_INTERNAL;

  void *ret;
  pthread_join (t->native, &ret);
  memset (&t->native, 0, sizeof t->native);
  return ENCA_OK;
}

enca_result
enca_thread_detach (enca_thread *t)
{
  if (!t)
    return ENCA_ERR_INVALID_ARGUMENT;

  pthread_detach (t->native);
  t->detached = true;
  return ENCA_OK;
}

enca_result
enca_mutex_init (enca_mutex *m)
{
  if (!m)
    return ENCA_ERR_INVALID_ARGUMENT;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
  int rc = pthread_mutex_init (&m->native, &attr);
  pthread_mutexattr_destroy (&attr);

  return rc == 0 ? ENCA_OK : ENCA_ERR_INTERNAL;
}

void
enca_mutex_destroy (enca_mutex *m)
{
  if (m)
    pthread_mutex_destroy ((pthread_mutex_t *) &m->native);
}

void
enca_mutex_lock (enca_mutex *m)
{
  pthread_mutex_lock ((pthread_mutex_t *) &m->native);
}

void
enca_mutex_unlock (enca_mutex *m)
{
  pthread_mutex_unlock ((pthread_mutex_t *) &m->native);
}

bool
enca_mutex_try_lock (enca_mutex *m)
{
  return pthread_mutex_trylock ((pthread_mutex_t *) &m->native) == 0;
}

enca_result
enca_condition_init (enca_condition *c)
{
  if (!c)
    return ENCA_ERR_INVALID_ARGUMENT;
  return pthread_cond_init (&c->native, NULL) == 0 ? ENCA_OK : ENCA_ERR_INTERNAL;
}

void
enca_condition_destroy (enca_condition *c)
{
  if (c)
    pthread_cond_destroy (&c->native);
}

static void
abs_deadline_timespec (enca_u64 ns, struct timespec *ts)
{
  clock_gettime (CLOCK_REALTIME, ts);
  ts->tv_sec += (time_t) (ns / ENCA_NS_PER_S);
  ts->tv_nsec += (long) (ns % ENCA_NS_PER_S);
  while (ts->tv_nsec >= 1000000000L)
    {
      ts->tv_nsec -= 1000000000L;
      ts->tv_sec += 1;
    }
}

void
enca_condition_wait (enca_condition *c, enca_mutex *m)
{
  pthread_cond_wait (&c->native, (pthread_mutex_t *) &m->native);
}

bool
enca_condition_timed_wait (enca_condition *c, enca_mutex *m, enca_u64 ns)
{
  struct timespec ts;
  abs_deadline_timespec (ns, &ts);
  return pthread_cond_timedwait (&c->native,
                                 (pthread_mutex_t *) &m->native, &ts) == 0;
}

void
enca_condition_signal (enca_condition *c)
{
  pthread_cond_signal (&c->native);
}

void
enca_condition_broadcast (enca_condition *c)
{
  pthread_cond_broadcast (&c->native);
}

#endif
