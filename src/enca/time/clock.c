#include "time.h"

#if defined(_WIN32)

# define WIN32_LEAN_AND_MEAN
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>

static enca_u64 qpc_freq;

static void
qpc_init (void)
{
  LARGE_INTEGER f;
  QueryPerformanceFrequency (&f);
  qpc_freq = (enca_u64) f.QuadPart;
}

enca_timestamp_ns
enca_monotonic_now_ns (void)
{
  LARGE_INTEGER c;
  QueryPerformanceCounter (&c);

  if (!qpc_freq)
    qpc_init ();

  enca_u64 ticks = (enca_u64) c.QuadPart;
  return (ticks / qpc_freq) * ENCA_NS_PER_S
         + ((ticks % qpc_freq) * ENCA_NS_PER_S) / qpc_freq;
}

enca_timestamp_ns
enca_wallclock_now_ns (void)
{
  FILETIME ft;
# if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0602
  GetSystemTimePreciseAsFileTime (&ft);
# else
  GetSystemTimeAsFileTime (&ft);
# endif
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;

  const enca_u64 epoch_diff = 116444736000000000ULL;
  enca_u64 t100ns = u.QuadPart - epoch_diff;
  return t100ns * 100u;
}

enca_timestamp_ns
enca_thread_cpu_time_ns (void)
{
  FILETIME creation, exit, kernel, user;
  if (!GetThreadTimes (GetCurrentThread (), &creation, &exit, &kernel, &user))
    return 0;

  ULARGE_INTEGER k, u;
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;
  return (k.QuadPart + u.QuadPart) * 100u;
}

#else

# include <time.h>
# include <unistd.h>

enca_timestamp_ns
enca_monotonic_now_ns (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (enca_timestamp_ns) ts.tv_sec * ENCA_NS_PER_S + (enca_timestamp_ns) ts.tv_nsec;
}

enca_timestamp_ns
enca_wallclock_now_ns (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_REALTIME, &ts);
  return (enca_timestamp_ns) ts.tv_sec * ENCA_NS_PER_S + (enca_timestamp_ns) ts.tv_nsec;
}

enca_timestamp_ns
enca_thread_cpu_time_ns (void)
{
# ifdef CLOCK_THREAD_CPUTIME_ID
  struct timespec ts;
  if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
    return 0;
  return (enca_timestamp_ns) ts.tv_sec * ENCA_NS_PER_S + (enca_timestamp_ns) ts.tv_nsec;
# else
  return 0;
# endif
}

#endif
