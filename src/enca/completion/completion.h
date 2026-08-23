#ifndef ENCA_COMPLETION_H
#define ENCA_COMPLETION_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"
#include "../snapshot/snapshot.h"

/* EVS-4: Real Completion substrate (bench/enca/evs4/COMPLETION.md).

   A completion request NEVER receives the document -- only a snapshot
   reference plus a declared context range, read through the EXISTING
   generic text walk.  No new snapshot API; Range View stays banned
   until extraction is a proven bottleneck. */

typedef enum enca_ct_trigger
{
  ENCA_CT_MANUAL = 0,
  ENCA_CT_PREFIX = 1,           /* W1: foo.ba|                       */
  ENCA_CT_MEMBER = 2,           /* W2: object.foo|                   */
  ENCA_CT_ARG = 3,              /* W3: foo(arg1, |                   */
  ENCA_CT_SYNTAX = 4            /* W4: if (...) { fo|                */
} enca_ct_trigger;

#define ENCA_CT_MAX_CANDIDATES 16

typedef struct enca_ct_request enca_ct_request;

/* Build a request.  PREFIX (if any) and the snapshot reference are
   copied/borrowed per ownership rules: the request takes its own
   snapshot reference; the caller keeps theirs. */
enca_result enca_ct_request_create (const enca_document_snapshot *snap,
                                    enca_object_id document_id,
                                    enca_u64 generation,
                                    enca_ct_trigger trigger,
                                    size_t cursor,
                                    const void *prefix, size_t prefix_len,
                                    size_t ctx_start, size_t ctx_end,
                                    enca_ct_request **out);

void enca_ct_request_destroy (enca_ct_request *req);

const enca_document_snapshot *
enca_ct_request_snapshot (const enca_ct_request *req);

size_t enca_ct_request_cursor (const enca_ct_request *req);

enca_usize enca_ct_requests_live (void);

/* Extract ONLY [start, end) of the snapshot text into BUF.
   O(region + pieces-touched): the generic walk skips foreign pieces
   without copying them.  Works for flat and piece-backed storage.
   Offsets are clamped memmove-style. */
enca_result enca_ct_extract_context (const enca_document_snapshot *snap,
                                     size_t start, size_t end,
                                     unsigned char *buf, size_t buflen,
                                     size_t *out_len);

/* Synthetic completion server (deterministic; stands in for backend +
   ranking).  Reads ONLY the request's context range via the walk and
   derives up to OUT_COUNT candidates from it.  Returns ENCA_OK with
   candidates in OUT_HASHES.  EXTRACT_NS and SERVE_NS receive phase
   timings when non-NULL. */
/* Context window per trigger kind (contract section 4): the frozen
   W1-W4 mapping from cursor to [start, end). */
void enca_ct_context_window (enca_ct_trigger trigger, size_t cursor,
                             size_t *start, size_t *end);

enca_result enca_ct_synth_server (const enca_ct_request *req,
                                  unsigned *out_count,
                                  enca_u64 out_hashes[ENCA_CT_MAX_CANDIDATES],
                                  enca_u64 *extract_ns,
                                  enca_u64 *serve_ns);

/* ---------------- EVS-4.4 candidate model / popup layout ------------- */

/* A materialized candidate model (T6): labels + annotations copied
   into one owned block.  This is what an A2/A3 worker would produce
   off-thread; the main thread only installs it. */
typedef struct
{
  char *label;
  size_t label_len;
  char *annot;
  size_t annot_len;
} enca_ct_candidate;

typedef struct
{
  enca_ct_candidate *items;
  size_t count;
  size_t bytes;                 /* total payload allocated           */
} enca_ct_model;

/* Build COUNT candidates with labels of LABEL_LEN bytes and
   annotations of ANNOT_LEN bytes (0 = none), deterministically
   derived from SEED.  FILTER shapes the visible subset the way a
   completion frontend would (exact/prefix/fuzzy keep progressively
   fewer items). */
typedef enum
{
  ENCA_CTF_EXACT = 0,
  ENCA_CTF_PREFIX = 1,
  ENCA_CTF_FUZZY = 2
} enca_ct_filter;

enca_result enca_ct_model_build (size_t count, size_t label_len,
                                 size_t annot_len, enca_u64 seed,
                                 enca_ct_filter filter,
                                 enca_ct_model *out);
void enca_ct_model_destroy (enca_ct_model *m);

/* Popup layout (T7->T8): from a model, compute the visible geometry a
   frontend needs -- visible rows capped by MAX_ROWS, widest visible
   column, scroll offset for CURSOR_INDEX. */
typedef struct
{
  size_t visible_rows;
  size_t col_width;
  size_t first_visible;
} enca_ct_popup_layout;

void enca_ct_popup_layout_calc (const enca_ct_model *m, size_t max_rows,
                           size_t cursor_index, enca_ct_popup_layout *out);

#endif /* ENCA_COMPLETION_H */
