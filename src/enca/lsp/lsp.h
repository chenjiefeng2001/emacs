#ifndef ENCA_LSP_H
#define ENCA_LSP_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../id/id.h"

/* EVS-4.3: real LSP transport attribution (bench/enca/evs4/EVS43.md).

   Invariants frozen in the contract and enforced here:
   - LSP textDocument.version == ENCA document revision (2.1).
   - Commit eligibility is the four-term pure gate below; it is THE
     correctness mechanism.  $ / cancelRequest is an optimization only
     (2.3).
   - The scheduler never sees this layer; it is called from a completion
     executor on a worker thread.
   - Session lifecycle is independent of requests: spawn/handshake/
     didOpen happen once at setup, never inside the measured path. */

typedef enum
{
  ENCA_LSP_LOOPBACK = 0,        /* B1: real OS pipe, no process      */
  ENCA_LSP_CLANGD = 1           /* B2: real server                   */
} enca_lsp_mode;

typedef struct enca_lsp_session enca_lsp_session;

typedef struct
{
  const char *clangd_path;      /* required for ENCA_LSP_CLANGD      */
  const char *root_uri;         /* may be NULL                       */
  const char *language_id;      /* default "c"                       */
} enca_lsp_session_opts;

enca_result enca_lsp_session_create (enca_lsp_mode mode,
                                     const enca_lsp_session_opts *opts,
                                     enca_lsp_session **out);

void enca_lsp_session_destroy (enca_lsp_session *s);

/* Document sync.  VERSION is the caller's ENCA revision; the session
   forwards it verbatim as textDocument.version (invariant 2.1). */
enca_result enca_lsp_did_open (enca_lsp_session *s, const char *uri,
                               const char *text, size_t len,
                               enca_u64 version);
enca_result enca_lsp_did_change_full (enca_lsp_session *s,
                                      const char *uri, const char *text,
                                      size_t len, enca_u64 version);

/* Blocking completion round trip ON THE CALLING THREAD.  URI must be
   the one opened on this session.  On success *RESPONSE points at the
   session-owned payload buffer, valid until the next session call;
   RESPONSE_LEN is its byte size.  TIMING (optional) receives phase
   ns breakdowns per contract section 3. */
typedef struct
{
  enca_u64 serialize_ns;
  enca_u64 write_ns;
  enca_u64 roundtrip_ns;        /* write-complete -> frame read      */
  enca_u64 parse_ns;
  size_t request_bytes;
  size_t response_bytes;
} enca_lsp_timing;

enca_result enca_lsp_completion (enca_lsp_session *s, const char *uri,
                                 size_t line, size_t character,
                                 const char **response,
                                 size_t *response_len,
                                 enca_lsp_timing *timing);

/* Layer-2 cancellation (optimization only): fire-and-forget notify.
   Losing it never affects correctness (invariant 2.3). */
enca_result enca_lsp_cancel_last (enca_lsp_session *s);

/* Invariant 2.2: the commit gate.  Pure function; THE correctness
   mechanism for stale responses. */
bool enca_lsp_commit_eligible (enca_object_id rsp_doc, enca_u64 rsp_gen,
                               enca_u64 rsp_rev, bool cancelled,
                               enca_object_id cur_doc, enca_u64 cur_gen,
                               enca_u64 cur_rev);

/* ---------------- EVS-5 backend attribution primitives ------------ */

/* Fire a completion request WITHOUT waiting for its response
   (pipelined attribution experiments: storms, drain profiles). */
enca_result enca_lsp_send_completion (enca_lsp_session *s,
                                      const char *uri, size_t line,
                                      size_t character,
                                      enca_u64 *out_id);

/* Read the NEXT response frame; ARRIVE_NS receives the monotonic
   timestamp of arrival.  Validates ID matches EXPECT_ID when
   EXPECT_ID is non-zero. */
enca_result enca_lsp_collect_response (enca_lsp_session *s,
                                       enca_u64 expect_id,
                                       const char **response,
                                       size_t *response_len,
                                       enca_u64 *arrive_ns);

/* Collect/read idle deadline (attribution harnesses shorten it). */
void enca_lsp_set_collect_timeout (enca_lsp_session *s, unsigned ms);

/* Layer-2 cancellation for a SPECIFIC outstanding id. */
enca_result enca_lsp_cancel_id (enca_lsp_session *s, enca_u64 id);

/* clangd discovery helper for harnesses; NULL when absent. */
const char *enca_lsp_find_clangd (void);

#endif /* ENCA_LSP_H */
