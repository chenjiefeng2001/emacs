#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "transport.h"

#include "../time/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <errno.h>
# include <poll.h>
# include <unistd.h>
#endif

enca_result
enca_lsp_endpoint_pipe (enca_lsp_endpoint *read_end,
                        enca_lsp_endpoint *write_end,
                        unsigned suggested_bytes)
{
#ifdef _WIN32
  HANDLE rd = NULL, wr = NULL;
  SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; /* inheritable */
  if (!CreatePipe (&rd, &wr, &sa, (DWORD) suggested_bytes))
    return ENCA_ERR_CLOSED;
  read_end->h = rd;
  write_end->h = wr;
  return ENCA_OK;
#else
  int fds[2];
  if (pipe (fds) != 0)
    return ENCA_ERR_CLOSED;
  read_end->fd = fds[0];
  write_end->fd = fds[1];
  return ENCA_OK;
#endif
}

void
enca_lsp_endpoint_close (enca_lsp_endpoint *ep)
{
  if (!ep)
    return;
#ifdef _WIN32
  if (ep->h)
    {
      CloseHandle ((HANDLE) ep->h);
      ep->h = NULL;
    }
#else
  if (ep->fd >= 0)
    {
      close (ep->fd);
      ep->fd = -1;
    }
#endif
}

enca_result
enca_lsp_write_all (enca_lsp_endpoint *ep, const char *buf, size_t len)
{
  size_t done = 0;
#ifdef _WIN32
  HANDLE h = (HANDLE) ep->h;
  while (done < len)
    {
      DWORD wrote = 0;
      DWORD chunk = (DWORD) ((len - done) > (1u << 20)
                               ? (1u << 20)
                               : (len - done));
      if (!WriteFile (h, buf + done, chunk, &wrote, NULL) || wrote == 0)
        return ENCA_ERR_CLOSED;
      done += wrote;
    }
  return ENCA_OK;
#else
  while (done < len)
    {
      ssize_t w = write (ep->fd, buf + done, len - done);
      if (w < 0)
        {
          if (errno == EINTR)
            continue;
          return ENCA_ERR_CLOSED;
        }
      done += (size_t) w;
    }
  return ENCA_OK;
#endif
}

/* Wait until EP is readable or DEADLINE_NS passes.  Returns OK,
   ERR_TIMEOUT or ERR_IO.  Windows granularity here is the OS timer
   tick (~1ms): it guards only the clangd arm, where server latency
   dwarfs it; the loopback arm never waits (see read_exact). */
static enca_result
wait_readable (enca_lsp_endpoint *ep, enca_u64 deadline_ns)
{
#ifdef _WIN32
  /* Hybrid wait: sub-millisecond arrivals (loopback echo behind a
     shuffler thread) are caught by a short yield-spin so B1
     attribution stays at true microsecond scale; anything slower
     falls back to Sleep(1) polling, whose coarse granularity is
     irrelevant against real server latencies (clangd arm). */
  const enca_u64 spin_until_ns = enca_monotonic_now_ns () + 2000000ull;
  for (;;)
    {
      DWORD avail = 0;
      if (!PeekNamedPipe ((HANDLE) ep->h, NULL, 0, NULL, &avail, NULL))
        return ENCA_ERR_CLOSED;     /* peer closed / broken pipe */
      if (avail > 0)
        return ENCA_OK;
      enca_u64 now = enca_monotonic_now_ns ();
      if (now >= deadline_ns)
        return ENCA_ERR_TIMEOUT;
      if (now < spin_until_ns)
        SwitchToThread ();
      else
        Sleep (1);
    }
#else
  for (;;)
    {
      enca_u64 now = enca_monotonic_now_ns ();
      if (now >= deadline_ns)
        return ENCA_ERR_TIMEOUT;
      enca_u64 left = deadline_ns - now;
      long remain_ms = (long) ((left + 999999ull) / 1000000ull);\n      if (remain_ms > 60000)\n        remain_ms = 60000;
      struct pollfd pfd = { ep->fd, POLLIN, 0 };
      int pr = poll (&pfd, 1, (int) remain_ms);
      if (pr > 0)
        return ENCA_OK;
      if (pr == 0)
        return ENCA_ERR_TIMEOUT;
      if (errno != EINTR)
        return ENCA_ERR_CLOSED;
    }
#endif
}

enca_result
enca_lsp_read_some (enca_lsp_endpoint *ep, char *buf, size_t want,
                    size_t *got_out, enca_u64 deadline_ns)
{
#ifdef _WIN32
  /* Fast probe: when the kernel buffer already holds bytes (loopback:
     writer finished before we entered), skip the timed wait entirely
     so B1 attribution keeps microsecond resolution. */
  DWORD avail = 0;
  BOOL peek_ok = PeekNamedPipe ((HANDLE) ep->h, NULL, 0, NULL, &avail,
                                NULL);
  if (!peek_ok)
    return ENCA_ERR_CLOSED;
  if (avail == 0)
    {
      enca_result r = wait_readable (ep, deadline_ns);
      if (r != ENCA_OK)
        return r;
    }
#else
  enca_result r = wait_readable (ep, deadline_ns);
  if (r != ENCA_OK)
    return r;
#endif

#ifdef _WIN32
  DWORD n = 0;
  /* A pipe ReadFile returns as soon as ANY bytes are available; it
     does not wait for the full request. */
  BOOL rf_ok = ReadFile ((HANDLE) ep->h, buf, (DWORD) want, &n, NULL);
  if (!rf_ok)
    return ENCA_ERR_CLOSED;
  if (n == 0)
    return ENCA_ERR_CLOSED;
  *got_out = n;
  return ENCA_OK;
#else
  ssize_t rd = read (ep->fd, buf, want);
  if (rd < 0)
    {
      if (errno == EINTR)
        return wait_readable (ep, deadline_ns); /* caller retries */
      return ENCA_ERR_CLOSED;
    }
  if (rd == 0)
    return ENCA_ERR_CLOSED;     /* EOF */
  *got_out = (size_t) rd;
  return ENCA_OK;
#endif
}
