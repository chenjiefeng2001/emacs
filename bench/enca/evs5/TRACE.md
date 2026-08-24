# EVS-5.5 -- REAL COMPLETION/EDIT TRACE (contract, frozen 2026-08-25)

## 0. Question

> During realistic completion usage, what fraction of edits destroy the
> completion context, and what fraction is provably range-safe for
> cross-revision reuse?

This is a MEASUREMENT-ONLY phase: no cache behavior changes, no
Stage-D implementation, no tree-sitter/AST dependency analysis.  It
produces the decision input for Stage D that handoff section 5
candidate 1 has been waiting for.

## 1. Event model

Every completion request records:

    CompletionEvent { rev, prefix, cursor, ctx=(beg,end), result }

where ctx is the PURE-RANGE context span (v1 definition): the maximal
run of identifier-ish characters [A-Za-z0-9_.$] around the cursor.
prefix_range = [cursor - len(prefix), cursor) is tracked alongside.

Every scripted edit between two requests records:

    Edit { beg, del_len, ins_text, kind }

kind is injected by the harness driver (edit / undo / cursor-move /
document-replace), never inferred.

## 2. EditRelation (per edit, vs the LIVE last-completion context)

Precedence-ordered; the first matching rule wins:

    UNDO            kind == undo (explicit restore op)
    REDO            kind == redo
    DOCUMENT_REPLACE kind == replace or |delta_size| > 25% buffer
    CURSOR_ONLY     no text change (goto only)
    INSIDE_PREFIX   edit range overlaps prefix_range
    INSIDE_CONTEXT  edit range overlaps ctx but not prefix_range
    WHITESPACE      disjoint from ctx AND deleted+inserted text is
                    all whitespace
    BEFORE_CONTEXT  entirely before ctx
    AFTER_CONTEXT   entirely after ctx
    UNKNOWN         fallback (should not occur in scripted runs)

The stored context interval is translated through each edit
(standard interval adjustment) so subsequent relations stay in
original-content coordinates.

## 3. Request classification H0..H4

For request R_j, relative to the previous request R_i, with E = edits
strictly between them:

    H0   E empty, same rev/prefix/cursor          (current cache HIT)
    H0m  E empty, same rev, different key         (current cache MISS
          by key -- cursor/prefix moved; semantics unchanged)
    H1   E non-empty, ALL edits range-disjoint
         from ctx_i, same prefix                  (Stage-D candidate:
          cross-revision reuse of identical context content)
    H2   prefix identical, some edit overlaps ctx (needs stronger
          semantic judgment than ranges)
    H3   all edits disjoint from ctx, prefix differs, and nearest
         edit within NEAR=64B of ctx_i           (nearby context;
          banned for reuse in v1)
    H4   everything else                          (genuinely new)

Engine truth is recorded per request (hit / miss / nobackend source
label from enca-evs-complete) so classification can be cross-checked
against what the CURRENT cache actually did:
H0 must be engine-hit; H1/H2/H3/H4 must be engine-miss today.
Any violation is a harness bug (same discipline as false_hit=0).

Headline metric:

    potential_cross_revision_hit = H1_count / total_requests

plus the relation histogram and per-class engine-source agreement.

## 4. Workload cells (deterministic scripts, real clangd backend)

| cell | behavior | expected signal |
|---|---|---|
| A IDGROW | identifier typing chains: `al` -> `alp` -> ... -> `alpha_field` at one anchor | edits INSIDE_PREFIX/H4-heavy |
| B CALLARGS | argument-list growth: `proc(` -> `proc(a` -> `proc(a,` -> `proc(a, b` | inserts after ctx span => AFTER_CONTEXT/H1 candidates |
| C CURSOR | completions separated by pure point moves | H4-no-edit + H0m share (key invalidation without semantic change) |
| D FAREDIT | completion, then insert unrelated code far away, re-complete SAME prefix at shifted anchor | THE money cell: H1 share = direct Stage-D win rate |
| E SESSION | weighted interleave: ~40% D-style, ~25% A/B growth, ~15% C moves, ~10% retry(H0), ~10% undo/edit-inside | aggregate distribution under mixed behavior |

All cells run against the real clangd arm (enca-evs-start with server
path), single-line ASCII document as in EVS-5.4 (byte == UTF-16
offsets).  Every edit bumps revision + lsp-sync so clangd stays
version-bound.

Known v1 limitation (frozen on purpose): ctx is an identifier-run
span, NOT a syntactic region; CALLARGS inserts land after '(' i.e.
outside the token span.  This UNDERSTATES context sensitivity for
argument editing.  If the trace later shows range-based rules
mispricing many cells, THAT is the evidence gate for AST work --
never before.

## 5. Decision framework (NOT a fixed gate)

    Expected latency saving = P(H1) x backend_latency
                              - invalidation/reuse complexity cost
                              - memory cost
                              - correctness risk premium

backend_latency scenarios to evaluate side by side:
  toy-document real clangd ....... ~4.6ms  (EVS-5.4 measured)
  historical attribution load .... ~77ms   (B2, REPORT section 21)
  injected think-time ............ 90ms    (loopback arms)

20% P(H1) is a DISCUSSION LINE, not a Go/No-Go gate: if real-project
misses are ~80ms then even 20% safe reuse is worth having; if misses
are ~5ms it likely is not.  The honest statement this phase must
enable is exactly this multiplication -- nothing stronger.

Output lines:

    REL55|cell|relation|count
    CLS55|cell|hclass|count|engine_miss_count
    SUM55|cell|requests=|h0=|h0m=|h1=|h2=|h3=|h4=|safe_pct=|eng_hits=
    KPV55|cell|eng_ms|total_ms|src        (latency spot-check rows)

## 6. Outcome -- trace executed (2026-08-25)

Raw: bench/results/evs55_trace.log.  Harness: test/enca/evs55-trace.el
(pure elisp, ZERO cache/engine changes); runner:
bench/enca/evs55_run.sh.  Real clangd 18.1.3 backend, tty.

| cell | requests | h0 | h0m | h1 | h2 | h3 | h4 | safe_pct | eng_hits |
|---|---|---|---|---|---|---|---|---|---|
| IDGROW (typing chain) | 13 | 1 | 0 | 0 | 0 | 0 | 12 | 0.0% | 1 |
| CALLARGS (arg growth) | 5 | 0 | 0 | 4 | 0 | 0 | 1 | **80.0%** | 0 |
| CURSOR (moves only) | 5 | 0 | 4 | 0 | 0 | 0 | 1 | 0.0% | 2 |
| FAREDIT (unrelated edits) | 11 | 0 | 0 | 10 | 0 | 0 | 1 | **90.9%** | 0 |
| SESSION (mixed weights) | 26 | 0 | 10 | 4 | 1 | 0 | 11 | **15.4%** | 5 |

Relation histograms: IDGROW INSIDE_PREFIX x11; CALLARGS AFTER_CONTEXT
x4; FAREDIT AFTER_CONTEXT x10; SESSION AFTER_CONTEXT x10 +
INSIDE_PREFIX x5 + UNDO x5.

### 6.1 Findings

1. **Behavior-conditional reuse ceiling is extremely bimodal.**
   Typing chains are 0% reusable (every keystroke mutates the token --
   correctly H4/H2 territory).  Unrelated-region editing is 91%
   range-safe and argument-list growth 80% -- these are the behaviors
   Stage D would monetize.  The mixed synthetic session lands at 15.4%,
   below the 20% discussion line, but its weights are OUR script's,
   not a user's.
2. **H0m discovery (cursor fragmentation).** Pure point movement
   between completions changes the cache key (cursor is keyed) even
   though semantics are untouched: 14 H0m requests overall, of which
   7 still HIT because the exact key repeated earlier in the same
   revision.  A key-normalization layer (e.g. token-relative cursor)
   could capture part of this WITHOUT any cross-revision machinery --
   potentially cheaper than Stage D for the same visible win.  New
   input for the Stage-D cost side of the formula.
3. **UNDO behaves as designed**: 5/5 classified conservative-H4
   (unsafe).  Any future cross-revision rule must either prove undo
   restores an exact prior state or keep paying full misses.
4. **Engine-truth agreement holds everywhere**: every H1/H2/H4 was an
   engine miss today; every engine hit was H0 or a same-revision
   exact-key repeat (the refined H0m reading above).  No false hits;
   no harness/classifier contradiction remains.

### 6.2 Decision math (per section 5; discussion, NOT a gate)

    Expected saving = P(H1) x backend_latency - complexity costs

| behavior mix | P(H1) | x toy-doc 4.6ms | x real-project ~80ms |
|---|---|---|---|
| typing-only (IDGROW) | 0.00 | 0 | 0 |
| arg-growth (CALLARGS) | 0.80 | 3.7ms | ~64ms |
| unrelated-edit (FAREDIT) | 0.91 | 4.2ms | ~73ms |
| synthetic mixed (SESSION) | 0.15 | 0.7ms | ~12.3ms |

Reading: at toy-document latency NO variant clears any reasonable
complexity bar; at realistic-project latency the far-edit behavior is
worth ~73ms per qualifying request IF such requests occur often
enough in real usage.  Both multipliers remain unmeasured in the wild:
(a) real users' edit-after-completion behavior mix, (b) real-project
backend latency on THIS machine.  Until (a) exists, Stage D stays
closed exactly per handoff section 5 candidate 1 -- but it now has
a working measurement instrument, precise class definitions, and
per-behavior upper bounds instead of a blank page.

### 6.3 Caveats (frozen honesty)

- Weights in SESSION are scripted, synthetic; they demonstrate the
  classifier, not user behavior.
- Toy document (single line, no includes): absolute latencies are not
  comparable to B2's real-project figures (see EVS-5.4 section 6).
- v1 ctx = identifier-run span; CALLARGS inserts after '(' sit outside
  it (documented limitation).  If range-based rules ever show large
  mispricing HERE first, that is the evidence gate for AST work.
- Pairwise-relative classification: H0m means "different key than the
  immediately preceding request"; some such requests still hit via
  exact-key repeats from earlier same-revision requests (observed,
  counted in eng_hits).
