#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "completion.h"

#include "../memory/memory.h"
#include "../time/time.h"

#include <string.h>

struct enca_ct_request
{
  enca_document_snapshot *snap; /* owned reference                   */
  enca_object_id document_id;
  enca_u64 generation;
  enca_ct_trigger trigger;
  size_t cursor;
  unsigned char *prefix;
  size_t prefix_len;
  size_t ctx_start, ctx_end;    /* clamped at creation               */
};

static _Atomic enca_usize ct_live_requests;

enca_result
enca_ct_request_create (const enca_document_snapshot *snap,
                        enca_object_id document_id,
                        enca_u64 generation,
                        enca_ct_trigger trigger,
                        size_t cursor,
                        const void *prefix, size_t prefix_len,
                        size_t ctx_start, size_t ctx_end,
                        enca_ct_request **out)
{
  if (!snap || !out)
    return ENCA_ERR_INVALID_ARGUMENT;

  size_t doc_len = (size_t) snap->text.len;

  enca_ct_request *r = enca_malloc (sizeof *r);
  if (!r)
    return ENCA_ERR_OUT_OF_MEMORY;
  memset (r, 0, sizeof *r);

  r->snap = enca_snapshot_acquire ((enca_document_snapshot *) snap);
  if (!r->snap)
    {
      enca_free (r);
      return ENCA_ERR_INVALID_ARGUMENT;
    }
  r->document_id = document_id;
  r->generation = generation;
  r->trigger = trigger;

  /* Memmove-style clamping, identical to EVS-2 semantics. */
  r->cursor = cursor > doc_len ? doc_len : cursor;
  r->ctx_start = ctx_start > doc_len ? doc_len : ctx_start;
  r->ctx_end = ctx_end > doc_len ? doc_len : ctx_end;
  if (r->ctx_start > r->ctx_end)
    {
      size_t t = r->ctx_start;
      r->ctx_start = r->ctx_end;
      r->ctx_end = t;
    }

  if (prefix_len > 0)
    {
      r->prefix = enca_malloc (prefix_len);
      if (!r->prefix)
        {
          enca_snapshot_release (r->snap);
          enca_free (r);
          return ENCA_ERR_OUT_OF_MEMORY;
        }
      memcpy (r->prefix, prefix, prefix_len);
      r->prefix_len = prefix_len;
    }

  atomic_fetch_add (&ct_live_requests, 1);
  *out = r;
  return ENCA_OK;
}

void
enca_ct_request_destroy (enca_ct_request *req)
{
  if (!req)
    return;
  enca_snapshot_release (req->snap);
  enca_free (req->prefix);
  enca_free (req);
  atomic_fetch_sub (&ct_live_requests, 1);
}

const enca_document_snapshot *
enca_ct_request_snapshot (const enca_ct_request *req)
{
  return req ? req->snap : NULL;
}

size_t
enca_ct_request_cursor (const enca_ct_request *req)
{
  return req ? req->cursor : 0;
}

enca_usize
enca_ct_requests_live (void)
{
  return atomic_load (&ct_live_requests);
}

/* ---- region extraction over the EXISTING walk ---- */

typedef struct
{
  size_t skip;                  /* bytes to skip before copying      */
  unsigned char *out;
  size_t buflen;
  size_t got;
} ct_span_ctx;

static bool
ct_span_walk (const unsigned char *data, size_t len, void *p)
{
  ct_span_ctx *c = p;
  if (c->got >= c->buflen)
    return false;               /* range complete: stop the walk     */
  if (c->skip >= len)
    {
      c->skip -= len;           /* foreign piece: skip without copy  */
      return true;
    }
  size_t take = len - c->skip;
  if (take > c->buflen - c->got)
    take = c->buflen - c->got;
  memcpy (c->out + c->got, data + c->skip, take);
  c->got += take;
  c->skip = 0;
  return c->got < c->buflen;
}

enca_result
enca_ct_extract_context (const enca_document_snapshot *snap,
                         size_t start, size_t end,
                         unsigned char *buf, size_t buflen,
                         size_t *out_len)
{
  if (!snap || !buf || buflen == 0)
    return ENCA_ERR_INVALID_ARGUMENT;
  if (end < start)
    end = start;
  size_t want = end - start;
  if (want > buflen)
    want = buflen;

  ct_span_ctx c = { start, buf, want, 0 };
  enca_snapshot_walk_text (snap, ct_span_walk, &c);
  *out_len = c.got;
  return ENCA_OK;
}

/* ---- synthetic server ---- */

/* Context window per trigger kind, per contract section 4. */
void
enca_ct_context_window (enca_ct_trigger trigger, size_t cursor,
                        size_t *start, size_t *end)
{
  static const size_t back[] = {
    [ENCA_CT_MANUAL] = 32,
    [ENCA_CT_PREFIX] = 32,      /* W1 */
    [ENCA_CT_MEMBER] = 256,     /* W2 */
    [ENCA_CT_ARG] = 1024,       /* W3 */
    [ENCA_CT_SYNTAX] = 4096,    /* W4 */
  };
  size_t b = back[trigger];
  *end = cursor;
  *start = cursor > b ? cursor - b : 0;
}

enca_result
enca_ct_synth_server (const enca_ct_request *req,
                      unsigned *out_count,
                      enca_u64 out_hashes[ENCA_CT_MAX_CANDIDATES],
                      enca_u64 *extract_ns, enca_u64 *serve_ns)
{
  if (!req || !out_count || !out_hashes)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_u64 t0 = enca_monotonic_now_ns ();

  unsigned char buf[4096];
  size_t len = 0;
  enca_result r = enca_ct_extract_context (
    req->snap, req->ctx_start, req->ctx_end, buf, sizeof buf, &len);
  if (r != ENCA_OK)
    return r;

  enca_u64 t1 = enca_monotonic_now_ns ();

  /* Deterministic candidates: seed from extracted context + prefix +
     trigger, then derive symbol hashes.  Stands in for backend round
     trip and ranking. */
  enca_u64 h = (enca_u64) 1469598103934665603ull;
  h ^= (enca_u64) req->trigger * 0x9e3779b97f4a7c15ull;
  h ^= (enca_u64) len << 7;
  for (size_t i = 0; i < len; i++)
    {
      h ^= buf[i];
      h *= (enca_u64) 1099511628211ull;
    }
  for (size_t i = 0; i < req->prefix_len && i < 64; i++)
    {
      h ^= req->prefix[i];
      h *= (enca_u64) 1099511628211ull;
    }

  unsigned n = 5 + (unsigned) (len % (ENCA_CT_MAX_CANDIDATES - 5));
  for (unsigned i = 0; i < n; i++)
    {
      h ^= h >> 31;
      h *= 0xbf58476d1ce4e5b9ull;
      out_hashes[i] = h ^ (enca_u64) i;
    }
  *out_count = n;

  enca_u64 t2 = enca_monotonic_now_ns ();
  if (extract_ns)
    *extract_ns = t1 - t0;
  if (serve_ns)
    *serve_ns = t2 - t1;
  return ENCA_OK;
}
