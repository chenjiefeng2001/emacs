#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "lsp.h"

#include "jsonrpc.h"
#include "transport.h"

#include "../time/time.h"
#include "../thread/thread.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <unistd.h>
#endif

#define LSP_DEADLINE_NS (30000ull * 1000000ull) /* 30s hard guard */

/* Per-session idle deadline for response collection. */
static unsigned lsp_collect_timeout_ms = 30000;

struct enca_lsp_session
{
  enca_lsp_mode mode;

  enca_lsp_endpoint wr;         /* we write here (server stdin)      */
  enca_lsp_endpoint rd;         /* we read here (server stdout)      */
#ifdef _WIN32
  void *proc;
#else
  int pid;
#endif
  bool has_proc;

  /* Loopback plumbing: a shuffler thread echoes bytes from mid_rd to
     mid_wr so writes never block on an unread pipe while still going
     through real kernel I/O (B1 arm). */
  enca_lsp_endpoint mid_rd, mid_wr;
  enca_thread shuffler;
  bool has_shuffler;

  enca_json_buf out;            /* serialize scratch                 */
  char *acc;                    /* read accumulator / frame buffer   */
  size_t acc_len, acc_cap;
  char *resp;                   /* last response copy (stable out)   */
  size_t resp_len, resp_cap;

  char uri[160];
  char language[32];
  enca_u64 next_id;

  /* Flow accounting for storm experiments (contract section 4). */
  enca_u64 requests_sent;
  enca_u64 responses_received;
  unsigned backend_delay_ms;
};

static enca_result handshake (enca_lsp_session *s, const char *root_uri);
static enca_result drain_echo_if_loopback (enca_lsp_session *s,
                                     enca_result send_r);

/* ---------------- process spawn (clangd arm) ---------------- */

static enca_result
spawn_clangd (enca_lsp_session *s, const char *path,
              char *const *extra_argv,
              enca_lsp_endpoint child_stdin, enca_lsp_endpoint child_stdout)
{
#ifdef _WIN32
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  char cmd[512];
  memset (&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = (HANDLE) child_stdin.h;
  si.hStdOutput = (HANDLE) child_stdout.h;
  si.hStdError = GetStdHandle (STD_ERROR_HANDLE);
  {
    size_t off
      = (size_t) snprintf (cmd, sizeof cmd, "\"%s\" --log=error", path);
    if (extra_argv)
      for (char *const *a = extra_argv; *a && off < sizeof cmd; a++)
        off += (size_t) snprintf (cmd + off, sizeof cmd - off,
                                  " \"%s\"", *a);
  }
  if (!CreateProcessA (NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si,
                       &pi))
    return ENCA_ERR_CLOSED;
  CloseHandle (pi.hThread);
  s->proc = pi.hProcess;
  s->has_proc = true;
  return ENCA_OK;
#else
  int pid = fork ();
  if (pid < 0)
    return ENCA_ERR_CLOSED;
  if (pid == 0)
    {
      dup2 (child_stdin.fd, 0);
      dup2 (child_stdout.fd, 1);
      char *argv[32];
      int argc = 0;
      argv[argc++] = (char *) path;
      argv[argc++] = (char *) "--log=error";
      if (extra_argv)
        for (char *const *a = extra_argv; *a && argc < 30; a++)
          argv[argc++] = *a;
      argv[argc] = NULL;
      {
        FILE *dbg = fopen ("/tmp/exec_dbg.txt", "a");
        if (dbg)
          {
            fprintf (dbg, "execvp path=%s argc=%d\n", path, argc);
            fclose (dbg);
          }
      }
      execvp (path, argv);
      {
        FILE *dbg = fopen ("/tmp/exec_dbg.txt", "a");
        if (dbg)
          {
            fprintf (dbg, "execvp FAILED errno=%d (%s)\n",
                     errno, strerror (errno));
            fclose (dbg);
          }
      }
      _exit (127);
    }
  s->pid = pid;
  s->has_proc = true;
  return ENCA_OK;
#endif
}

/* ---------------- framed IO ---------------- */

static enca_result
send_frame (enca_lsp_session *s, const char *payload, size_t len,
            enca_u64 *write_ns, size_t *bytes_out)
{
  char header[64];
  int hl = snprintf (header, sizeof header,
                     "Content-Length: %zu\r\n\r\n", len);
  if (hl <= 0)
    return ENCA_ERR_CLOSED;

  const enca_u64 t0 = enca_monotonic_now_ns ();
  enca_result r = enca_lsp_write_all (&s->wr, header, (size_t) hl);
  if (r == ENCA_OK && len > 0)
    r = enca_lsp_write_all (&s->wr, payload, len);
  const enca_u64 t1 = enca_monotonic_now_ns ();

  if (r != ENCA_OK)
    return r;
  if (write_ns)
    *write_ns = t1 - t0;
  if (bytes_out)
    *bytes_out = len + (size_t) hl;
  s->requests_sent++;
  return ENCA_OK;
}

/* Read one LSP frame into the session accumulator.  On success
   PAYLOAD and PAYLOAD_LEN point inside ACC. */
static enca_result
read_frame (enca_lsp_session *s, const char **payload,
            size_t *payload_len, enca_u64 *roundtrip_ns,
            const enca_u64 t_write_done)
{
  size_t scanned = 0;
  const enca_u64 deadline = enca_monotonic_now_ns ()
    + (enca_u64) lsp_collect_timeout_ms * 1000000ull;

  for (;;)
    {
      /* Look for the end of the header block in what we have. */
      while (scanned + 4 <= s->acc_len)
        {
          if (s->acc[scanned] == '\r' && s->acc[scanned + 1] == '\n'
              && s->acc[scanned + 2] == '\r'
              && s->acc[scanned + 3] == '\n')
            {
              size_t body_off = scanned + 4;
              long cl = -1;
              {
                char saved = s->acc[scanned];
                s->acc[scanned] = 0; /* bound sscanf */
                char *clp = strstr (s->acc, "Content-Length:");
                if (!clp)
                  clp = strstr (s->acc, "content-length:");
                if (clp)
                  cl = atol (clp + strlen ("Content-Length:"));
                s->acc[scanned] = saved;
              }
              if (cl < 0)
                return ENCA_ERR_CLOSED;
              size_t want = body_off + (size_t) cl;

              if (want > s->acc_cap)
                {
                  size_t ncap = s->acc_cap ? s->acc_cap : 8192;
                  while (ncap < want)
                    ncap *= 2;
                  char *nb = realloc (s->acc, ncap);
                  if (!nb)
                    return ENCA_ERR_OUT_OF_MEMORY;
                  s->acc = nb;
                  s->acc_cap = ncap;
                }
              if (s->acc_len < want)
                {
                  /* Pull exactly the remaining body bytes: pipe reads
                     return partial data, so loop until complete. */
                  size_t got = 0;
                  while (s->acc_len < want)
                    {
                      enca_result rr = enca_lsp_read_some (
                        &s->rd, s->acc + s->acc_len, want - s->acc_len,
                        &got, deadline);
                      if (rr != ENCA_OK)
                        return rr;
                      if (got == 0)
                        return ENCA_ERR_CLOSED;
                      s->acc_len += got;
                    }
                }
              if (roundtrip_ns)
                *roundtrip_ns
                  = enca_monotonic_now_ns () - t_write_done;
              *payload = s->acc + body_off;
              *payload_len = (size_t) cl;
              /* Consume the frame: our protocol never pipelines, so
                 the accumulator starts empty next call. */
              s->acc_len = 0;
              s->responses_received++;
              return ENCA_OK;
            }
          scanned++;
        }

      /* Need more header/body bytes. */
      if (s->acc_len + 4096 > s->acc_cap)
        {
          size_t ncap = s->acc_cap ? s->acc_cap * 2 : 16384;
          char *nb = realloc (s->acc, ncap);
          if (!nb)
            return ENCA_ERR_OUT_OF_MEMORY;
          s->acc = nb;
          s->acc_cap = ncap;
        }
      size_t got = 0;
      enca_result rr = enca_lsp_read_some (&s->rd, s->acc + s->acc_len,
                                           4096, &got, deadline);
      if (rr != ENCA_OK)
        return rr;
      s->acc_len += got;
    }
}

/* ---------------- loopback shuffler ---------------- */

static enca_result
shuffle_fn (void *arg)
{
  enca_lsp_session *s = arg;
  char buf[8192];
  for (;;)
    {
      size_t got = 0;
      /* Effectively unbounded deadline: EOF (peer closed) ends it. */
      enca_result r = enca_lsp_read_some (&s->mid_rd, buf, sizeof buf,
                                          &got, (enca_u64) -1);
      if (r != ENCA_OK || got == 0)
        break;
      if (enca_lsp_write_all (&s->mid_wr, buf, got) != ENCA_OK)
        break;
    }
  return ENCA_OK;
}

/* ---------------- session lifecycle ---------------- */

enca_result
enca_lsp_session_create (enca_lsp_mode mode,
                         const enca_lsp_session_opts *opts,
                         enca_lsp_session **out)
{
  if (!out || (mode == ENCA_LSP_CLANGD && !opts))
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_lsp_session *s = calloc (1, sizeof *s);
  if (!s)
    return ENCA_ERR_OUT_OF_MEMORY;
  s->mode = mode;
  s->next_id = 1;
  strcpy (s->language, "c");

  if (jb_init (&s->out) != ENCA_OK)
    {
      free (s);
      return ENCA_ERR_OUT_OF_MEMORY;
    }

  if (mode == ENCA_LSP_LOOPBACK)
    {
      /* Two anonymous pipes + a shuffler thread: writes always have a
         reader, so large payloads never block on pipe capacity, and
         the measured path still crosses real kernel I/O (B1 arm). */
      enca_lsp_endpoint p1r, p2w;
      enca_result r = enca_lsp_endpoint_pipe (&p1r, &s->wr, 8u << 20);
      if (r != ENCA_OK)
        {
          jb_free (&s->out);
          free (s);
          return r;
        }
      r = enca_lsp_endpoint_pipe (&s->rd, &p2w, 8u << 20);
      if (r != ENCA_OK)
        {
          enca_lsp_endpoint_close (&p1r);
          enca_lsp_endpoint_close (&s->wr);
          jb_free (&s->out);
          free (s);
          return r;
        }
      s->mid_rd = p1r;
      s->mid_wr = p2w;
      s->has_shuffler = true;
      if (enca_thread_create (&s->shuffler, "lsp-loopback", shuffle_fn,
                              s)
          != ENCA_OK)
        {
          enca_lsp_endpoint_close (&p1r);
          enca_lsp_endpoint_close (&p2w);
          enca_lsp_endpoint_close (&s->rd);
          enca_lsp_endpoint_close (&s->wr);
          s->has_shuffler = false;
          jb_free (&s->out);
          free (s);
          return ENCA_ERR_CLOSED;
        }
      *out = s;
      return ENCA_OK;
    }

  if (!opts->clangd_path)
    {
      jb_free (&s->out);
      free (s);
      return ENCA_ERR_INVALID_ARGUMENT;
    }

  /* clangd: two pipes (our write -> its stdin; its stdout -> our
     read), then spawn, then the initialize handshake below. */
  enca_lsp_endpoint child_stdin_rd, child_stdout_wr;
  enca_result r = enca_lsp_endpoint_pipe (&child_stdin_rd, &s->wr, 0);
  if (r != ENCA_OK)
    goto fail;
  r = enca_lsp_endpoint_pipe (&s->rd, &child_stdout_wr, 0);
  if (r != ENCA_OK)
    {
      enca_lsp_endpoint_close (&child_stdin_rd);
      goto fail;
    }
  r = spawn_clangd (s, opts->clangd_path, opts->exec_argv,
                    child_stdin_rd, child_stdout_wr);
  enca_lsp_endpoint_close (&child_stdin_rd);
  enca_lsp_endpoint_close (&child_stdout_wr);
  if (r != ENCA_OK)
    goto fail;

  /* Lifecycle independence (contract section 1): the initialize /
     initialized exchange happens HERE at setup, never inside the
     measured per-request path. */
  r = handshake (s, opts->root_uri);
  if (r != ENCA_OK)
    goto fail;

  *out = s;
  return ENCA_OK;

fail:
  jb_free (&s->out);
  enca_lsp_endpoint_close (&s->rd);
  enca_lsp_endpoint_close (&s->wr);
  free (s);
  return r;
}

void
enca_lsp_session_destroy (enca_lsp_session *s)
{
  if (!s)
    return;

  if (s->has_proc)
    {
#ifdef _WIN32
      HANDLE h = (HANDLE) s->proc;
      /* EOF on stdin signals graceful clangd shutdown first. */
      enca_lsp_endpoint_close (&s->wr);
      if (WaitForSingleObject (h, 2000) != WAIT_OBJECT_0)
        TerminateProcess (h, 1);
      CloseHandle (h);
#else
      enca_lsp_endpoint_close (&s->wr);
      kill (s->pid, SIGTERM);
      waitpid (s->pid, NULL, 0);
#endif
    }
  else if (s->has_shuffler)
    {
      /* Closing our write end sends EOF through the shuffler. */
      enca_lsp_endpoint_close (&s->wr);
      enca_thread_join (&s->shuffler);
      s->has_shuffler = false;
    }

  enca_lsp_endpoint_close (&s->mid_rd);
  enca_lsp_endpoint_close (&s->mid_wr);
  enca_lsp_endpoint_close (&s->rd);
  enca_lsp_endpoint_close (&s->wr);
  jb_free (&s->out);
  free (s->acc);
  free (s->resp);
  free (s);
}

/* ---------------- notifications & requests ---------------- */

static enca_result
send_notification (enca_lsp_session *s, const char *method,
                   const char *params_json)
{
  jb_reset_keep_cap (&s->out);
  enca_result r = jb_printf (
    &s->out, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}",
    method, params_json);
  if (r != ENCA_OK)
    return r;
  r = send_frame (s, s->out.buf, s->out.len, NULL, NULL);
  return drain_echo_if_loopback (s, r);
}

/* LOOPBACK echo discipline: every frame written is echoed back by the
   shuffler.  Notifications (no id) must consume their echo or the
   next request would read it as its response. */
static enca_result
drain_echo_if_loopback (enca_lsp_session *s, enca_result send_r)
{
  if (send_r != ENCA_OK || s->mode != ENCA_LSP_LOOPBACK)
    return send_r;
  const char *p = NULL;
  size_t l = 0;
  return read_frame (s, &p, &l, NULL, enca_monotonic_now_ns ());
}

static enca_result
handshake (enca_lsp_session *s, const char *root_uri)
{
  /* initialize */
  jb_reset_keep_cap (&s->out);
  enca_result r;
  if (root_uri)
    r = jb_printf (&s->out,
                   "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":"
                   "\"initialize\",\"params\":{\"processId\":null,"
                   "\"rootUri\":\"%s\",\"capabilities\":{}}}",
                   (unsigned long long) s->next_id++, root_uri);
  else
    r = jb_printf (&s->out,
                   "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":"
                   "\"initialize\",\"params\":{\"processId\":null,"
                   "\"rootUri\":null,\"capabilities\":{}}}",
                   (unsigned long long) s->next_id++);
  if (r != ENCA_OK)
    return r;

  const char *resp = NULL;
  size_t rl = 0;
  enca_u64 wns;
  size_t bytes;
  r = send_frame (s, s->out.buf, s->out.len, &wns, &bytes);
  if (r != ENCA_OK)
    return r;
  r = read_frame (s, &resp, &rl, NULL, 0);
  if (r != ENCA_OK)
    return r;
  if (!enca_json_has_member (resp, rl, "result"))
    return ENCA_ERR_CLOSED;

  /* initialized notification */
  return send_notification (s, "initialized", "{}");
}

enca_result
enca_lsp_did_open (enca_lsp_session *s, const char *uri, const char *text,
                   size_t len, enca_u64 version)
{
  if (!s || !uri || !text || len == 0)
    return ENCA_ERR_INVALID_ARGUMENT;
  snprintf (s->uri, sizeof s->uri, "%s", uri);

  jb_reset_keep_cap (&s->out);
  enca_result r
    = jb_puts (&s->out, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didOpen\",\"params\":{\"textDocument\":{\"uri\":");
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, uri, strlen (uri));
  if (r != ENCA_OK)
    return r;
  r = jb_printf (&s->out,
                 ",\"languageId\":\"%s\",\"version\":%llu,\"text\":",
                 s->language, (unsigned long long) version);
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, text, len);
  if (r != ENCA_OK)
    return r;
  r = jb_puts (&s->out, "}}}");
  if (r != ENCA_OK)
    return r;

  /* Setup cost: measured by the caller around this call; never part
     of the per-request path (contract section 3). */
  return drain_echo_if_loopback (s, send_frame (s, s->out.buf, s->out.len, NULL, NULL));
}

enca_result
enca_lsp_did_change_full (enca_lsp_session *s, const char *uri,
                          const char *text, size_t len, enca_u64 version)
{
  if (!s || !uri || !text || len == 0)
    return ENCA_ERR_INVALID_ARGUMENT;

  jb_reset_keep_cap (&s->out);
  enca_result r
    = jb_puts (&s->out, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didChange\",\"params\":{\"textDocument\":{\"uri\":");
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, uri, strlen (uri));
  if (r != ENCA_OK)
    return r;
  r = jb_printf (&s->out, ",\"version\":%llu},\"contentChanges\":[{\"text\":",
                 (unsigned long long) version);
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, text, len);
  if (r != ENCA_OK)
    return r;
  /* Close: change object, contentChanges array, params, request. */
  r = jb_puts (&s->out, "}]}}");
  if (r != ENCA_OK)
    return r;

  return drain_echo_if_loopback (s,
                                 send_frame (s, s->out.buf, s->out.len,
                                             NULL, NULL));
}

/* Send ONE request and read ITS response frame back.  On success the
   caller view points at a stable session-owned copy. */
static enca_result
round_trip (enca_lsp_session *s, const char *payload, size_t len,
            enca_u64 expect_id, const char **response, size_t *response_len,
            enca_lsp_timing *tm)
{
  enca_u64 wns = 0;
  size_t bytes = 0;
  const enca_u64 t0 = enca_monotonic_now_ns ();
  enca_result r = send_frame (s, payload, len, &wns, &bytes);
  if (r != ENCA_OK)
    return r;
  const enca_u64 t1 = enca_monotonic_now_ns ();

  /* Simulated backend think-time (LOOPBACK attribution): injected
     between write-complete and response-read so the request-response
     path pays realistic backend cost. */
  if (s->mode == ENCA_LSP_LOOPBACK && s->backend_delay_ms)
    {
      struct timespec ts;
      ts.tv_sec = (time_t) (s->backend_delay_ms / 1000);
      ts.tv_nsec = (long) (s->backend_delay_ms % 1000) * 1000000L;
      nanosleep (&ts, NULL);
    }

  /* Skip server notifications until the frame carrying EXPECT_ID
     arrives (publishDiagnostics etc. interleave freely). */
  const char *rp = NULL;
  size_t rl = 0;
  enca_u64 rt = 0;
  for (;;)
    {
      enca_result rr = read_frame (s, &rp, &rl, &rt, t1);
      if (rr != ENCA_OK)
        return rr;
      enca_u64 rid = 0;
      if (enca_json_get_u64 (rp, rl, "id", &rid) && rid == expect_id)
        break;
    }
  const enca_u64 t2 = enca_monotonic_now_ns ();

  enca_u64 rid = 0;
  bool have_id = enca_json_get_u64 (rp, rl, "id", &rid);
  /* LOOPBACK echoes the request verbatim: there is a matching id but
     no "result" member.  A real server must present one. */
  bool ok = have_id && rid == expect_id
            && (s->mode == ENCA_LSP_LOOPBACK
                || enca_json_has_member (rp, rl, "result"));
  const enca_u64 t3 = enca_monotonic_now_ns ();

  if (true)
    {
      /* Always expose the raw frame for diagnostics/harnesses, even
         when gate validation fails. */
      if (s->resp_cap < rl)
        {
          char *nb = realloc (s->resp, rl);
          if (!nb)
            return ENCA_ERR_OUT_OF_MEMORY;
          s->resp = nb;
          s->resp_cap = rl;
        }
      memcpy (s->resp, rp, rl);
      s->resp_len = rl;
      if (response)
        *response = s->resp;
      if (response_len)
        *response_len = rl;
    }

  if (tm)
    {
      tm->serialize_ns = 0;
      tm->write_ns = wns;
      tm->roundtrip_ns = rt > (t1 - t0) ? rt - (t1 - t0) : rt;
      tm->parse_ns = t3 - t2;
      tm->request_bytes = bytes;
      tm->response_bytes = ok ? rl : 0;
    }
  return ok ? ENCA_OK : ENCA_ERR_CLOSED;
}

enca_result
enca_lsp_completion (enca_lsp_session *s, const char *uri, size_t line,
                     size_t character, const char **response,
                     size_t *response_len, enca_lsp_timing *timing)
{
  if (!s || !uri)
    return ENCA_ERR_INVALID_ARGUMENT;

  const enca_u64 t0 = enca_monotonic_now_ns ();
  jb_reset_keep_cap (&s->out);
  enca_u64 id = s->next_id++;
  enca_result r = jb_printf (
    &s->out, "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"textDocument/"
             "completion\",\"params\":{\"textDocument\":{\"uri\":",
    (unsigned long long) id);
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, uri, strlen (uri));
  if (r != ENCA_OK)
    return r;
  r = jb_printf (&s->out,
                 "},\"position\":{\"line\":%zu,\"character\":%zu}}}",
                 line, character);
  if (r != ENCA_OK)
    return r;
  const enca_u64 t1 = enca_monotonic_now_ns ();

  enca_lsp_timing tm;
  memset (&tm, 0, sizeof tm);
  const char *resp = NULL;
  size_t rl = 0;
  r = round_trip (s, s->out.buf, s->out.len, id, &resp, &rl, &tm);
  const enca_u64 t4 = enca_monotonic_now_ns ();

  if (timing && r == ENCA_OK)
    {
      *timing = tm;
      timing->serialize_ns = t1 - t0;
      enca_u64 known = tm.write_ns + tm.roundtrip_ns + tm.parse_ns;
      timing->parse_ns += (t4 - t0) > known ? (t4 - t0) - known : 0;
    }
  if (r == ENCA_OK && response)
    *response = resp;
  if (r == ENCA_OK && response_len)
    *response_len = rl;
  return r;
}

enca_result
enca_lsp_cancel_last (enca_lsp_session *s)
{
  if (!s || s->next_id == 0)
    return ENCA_ERR_INVALID_ARGUMENT;
  jb_reset_keep_cap (&s->out);
  enca_result r = jb_printf (
    &s->out, "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\","
             "\"params\":{\"id\":%llu}}",
    (unsigned long long) (s->next_id - 1));
  if (r != ENCA_OK)
    return r;
  /* Layer-2 optimization: fire-and-forget; losing it never affects
     correctness (invariant 2.3). */
  return drain_echo_if_loopback (s, send_frame (s, s->out.buf, s->out.len, NULL, NULL));
}

/* ---------------- commit gate (invariant 2.2) ---------------- */

bool
enca_lsp_commit_eligible (enca_object_id rsp_doc, enca_u64 rsp_gen,
                          enca_u64 rsp_rev, bool cancelled,
                          enca_object_id cur_doc, enca_u64 cur_gen,
                          enca_u64 cur_rev)
{
  return rsp_doc == cur_doc && rsp_gen == cur_gen && rsp_rev == cur_rev
         && !cancelled;
}

/* ---------------- clangd discovery ---------------- */

const char *
enca_lsp_find_clangd (void)
{
  static char found[512];
#ifdef _WIN32
  if (SearchPathA (NULL, "clangd", ".exe", sizeof found, found, NULL))
    return found;
  const char *cand[] = { "C:\\Program Files\\LLVM\\bin\\clangd.exe" };
  for (size_t i = 0; i < sizeof cand / sizeof cand[0]; i++)
    {
      DWORD a = GetFileAttributesA (cand[i]);
      if (a != INVALID_FILE_ATTRIBUTES)
        {
          snprintf (found, sizeof found, "%s", cand[i]);
          return found;
        }
    }
  return NULL;
#else
  const char *cand[] = { "/usr/bin/clangd", "/usr/local/bin/clangd" };
  for (size_t i = 0; i < sizeof cand / sizeof cand[0]; i++)
    if (access (cand[i], X_OK) == 0)
      {
        snprintf (found, sizeof found, "%s", cand[i]);
        return found;
      }
  return NULL;
#endif
}

/* ---------------- EVS-5 attribution primitives ---------------- */

enca_result
enca_lsp_send_completion (enca_lsp_session *s, const char *uri,
                          size_t line, size_t character, enca_u64 *out_id)
{
  if (!s || !uri || !out_id)
    return ENCA_ERR_INVALID_ARGUMENT;

  jb_reset_keep_cap (&s->out);
  enca_u64 id = s->next_id++;
  enca_result r = jb_printf (
    &s->out, "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"textDocument/"
             "completion\",\"params\":{\"textDocument\":{\"uri\":",
    (unsigned long long) id);
  if (r != ENCA_OK)
    return r;
  r = jb_put_json_string (&s->out, uri, strlen (uri));
  if (r != ENCA_OK)
    return r;
  r = jb_printf (&s->out,
                 "},\"position\":{\"line\":%zu,\"character\":%zu}}}",
                 line, character);
  if (r != ENCA_OK)
    return r;

  r = send_frame (s, s->out.buf, s->out.len, NULL, NULL);
  if (r == ENCA_OK)
    *out_id = id;
  return r;
}

enca_result
enca_lsp_collect_response (enca_lsp_session *s, enca_u64 expect_id,
                           const char **response, size_t *response_len,
                           enca_u64 *arrive_ns)
{
  /* LOOPBACK simulated think-time: injected BEFORE the reply is
     read so MISS-arm attribution includes realistic backend cost. */
  if (s->mode == ENCA_LSP_LOOPBACK && s->backend_delay_ms)
    {
      struct timespec ts;
      ts.tv_sec = (time_t) (s->backend_delay_ms / 1000);
      ts.tv_nsec = (long) (s->backend_delay_ms % 1000) * 1000000L;
      nanosleep (&ts, NULL);
    }
#ifdef LSP_DEBUG_DELAY
  else
    fprintf (stderr, "[dbg] collect no-delay mode=%d dly=%u\n",
             (int) s->mode, s->backend_delay_ms);
#endif

  /* Server-initiated NOTIFICATIONS (publishDiagnostics etc.) carry no
     id and must be skipped until the frame with the matching id
     arrives.  EXPECT_ID == 0 accepts any id-bearing response (storm
     attribution). */
  const enca_u64 deadline = enca_monotonic_now_ns ()
    + (enca_u64) lsp_collect_timeout_ms * 1000000ull;

  for (;;)
    {
      if (enca_monotonic_now_ns () >= deadline)
        return ENCA_ERR_TIMEOUT;

      const char *rp = NULL;
      size_t rl = 0;
      enca_result r = read_frame (s, &rp, &rl, NULL, deadline);
      if (r != ENCA_OK)
        return r;

      enca_u64 rid = 0;
      bool have_id = enca_json_get_u64 (rp, rl, "id", &rid);
      if (!have_id)
        continue;               /* notification: skip */

      if (expect_id != 0 && rid != expect_id)
        continue;               /* stale/mismatched id: keep scanning */

      bool ok = s->mode == ENCA_LSP_LOOPBACK
                || enca_json_has_member (rp, rl, "result");

      if (s->resp_cap < rl)
        {
          char *nb = realloc (s->resp, rl);
          if (!nb)
            return ENCA_ERR_OUT_OF_MEMORY;
          s->resp = nb;
          s->resp_cap = rl;
        }
      memcpy (s->resp, rp, rl);
      s->resp_len = rl;
      if (response)
        *response = s->resp;
      if (response_len)
        *response_len = rl;
      if (arrive_ns)
        *arrive_ns = enca_monotonic_now_ns ();
      s->responses_received++;
      return ok ? ENCA_OK : ENCA_ERR_CANCELLED;
    }
}

enca_result
enca_lsp_cancel_id (enca_lsp_session *s, enca_u64 id)
{
  if (!s)
    return ENCA_ERR_INVALID_ARGUMENT;
  jb_reset_keep_cap (&s->out);
  enca_result r = jb_printf (
    &s->out, "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\","
             "\"params\":{\"id\":%llu}}",
    (unsigned long long) id);
  if (r != ENCA_OK)
    return r;
  /* Layer-2 optimization: best effort.  In LOOPBACK mode the echo is
     drained to keep the stream aligned. */
  r = send_frame (s, s->out.buf, s->out.len, NULL, NULL);
  return drain_echo_if_loopback (s, r);
}

void
enca_lsp_set_collect_timeout (enca_lsp_session *s, unsigned ms)
{
  (void) s;                     /* process-global knob, deliberately */
  if (ms)
    lsp_collect_timeout_ms = ms;
}
void
enca_lsp_set_backend_delay_ms (enca_lsp_session *s, unsigned ms)
{
  if (s)
    {
      s->backend_delay_ms = ms;
      fprintf (stderr, "[dbg] set delay=%u\n", ms);
    }
}