#ifndef ENCA_LSP_JSONRPC_H
#define ENCA_LSP_JSONRPC_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

/* EVS-4.3 minimal JSON-RPC codec (contract section 1: smallest
   auditable codec that can frame OUR messages and extract the fields
   the commit gate needs).  Deliberately NOT a general JSON library.

   Writer: growable byte buffer + printf-style append + JSON string
   escaping.
   Scanner: finds a member by key anywhere in a payload and either
   reads an unsigned integer value or reports its presence; correctly
   skips strings (with escapes) and nested containers while searching. */

typedef struct
{
  char *buf;
  size_t len, cap;
} enca_json_buf;

void jb_reset_keep_cap (enca_json_buf *b);
enca_result jb_init (enca_json_buf *b);
void jb_free (enca_json_buf *b);
enca_result jb_puts (enca_json_buf *b, const char *s);
enca_result jb_put_bytes (enca_json_buf *b, const char *s, size_t n);
enca_result jb_printf (enca_json_buf *b, const char *fmt, ...);

/* Append S as a quoted JSON string with proper escapes (" \ and
   control bytes). */
enca_result jb_put_json_string (enca_json_buf *b, const char *s,
                                size_t len);

/* LSP framing: "Content-Length: N\r\n\r\n" prefix for PAYLOAD_LEN. */
enca_result jb_put_lsp_header (enca_json_buf *b, size_t payload_len);

/* Search PAYLOAD for member KEY at any depth and read its value as an
   unsigned integer.  Returns false when absent or non-numeric. */
bool enca_json_get_u64 (const char *payload, size_t len,
                        const char *key, enca_u64 *out);

/* True if member KEY exists anywhere in PAYLOAD. */
bool enca_json_has_member (const char *payload, size_t len,
                           const char *key);

#endif /* ENCA_LSP_JSONRPC_H */
