#ifndef ENCA_LSP_TRANSPORT_H
#define ENCA_LSP_TRANSPORT_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

/* EVS-4.3 byte-transport layer (bench/enca/evs4/EVS43.md).
   One endpoint is a single direction of an OS pipe.  Handles are
   opaque; only transport.c knows the platform. */

typedef struct
{
#ifdef _WIN32
  void *h;
#else
  int fd;
#endif
} enca_lsp_endpoint;

/* Anonymous OS pipe with both endpoints visible in-process: this IS
   the B1 loopback medium (real kernel I/O, no server process).
   SUGGESTED_BYTES sizes the kernel buffer; 0 = system default.  The
   loopback arm passes a large size because the echo consumer may lag
   an entire didOpen payload. */
enca_result enca_lsp_endpoint_pipe (enca_lsp_endpoint *read_end,
                                    enca_lsp_endpoint *write_end,
                                    unsigned suggested_bytes);

void enca_lsp_endpoint_close (enca_lsp_endpoint *ep);

/* Blocking write of exactly LEN bytes. */
enca_result enca_lsp_write_all (enca_lsp_endpoint *ep, const char *buf,
                                size_t len);

/* Read UP TO WANT bytes (at least one) into BUF; GOT receives the count. DEADLINE_NS bounds the wait for the FIRST byte only. */
/* Read UP TO WANT bytes (at least one) into BUF; GOT receives the
   count.  DEADLINE_NS bounds the wait for the FIRST byte only. */
enca_result enca_lsp_read_some (enca_lsp_endpoint *ep, char *buf,
                                size_t want, size_t *got,
                                enca_u64 deadline_ns);

#endif /* ENCA_LSP_TRANSPORT_H */
