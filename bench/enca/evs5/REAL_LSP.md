# EVS-5.4 -- REAL LSP THROUGH THE ELISP USER PATH
# (contract, frozen 2026-08-25; closes handoff section 5 candidate 3)

## 1. Question

> With the fake loopback backend (90ms injected think-time) replaced by
> a real clangd, what is the honest keypress->visible latency per
> real-typing class through the full elisp user path
> (engine -> popup -> redisplay)?

EVS-4.3 measured B2 round-trip p50=77ms at the C level (test_lsp.c).
Every cache/UI phase since has used loopback injection for the MISS
arm.  This phase removes that last synthetic element from the user
path.

## 2. Wiring contract

- `enca-evs-start WORKERS INCREMENTAL BACKEND`: BACKEND string = path
  to a real LSP server executable => ENCA_LSP_CLANGD session (spawn +
  initialize handshake at START time, never in the measured path).
  BACKEND `loopback` keeps the existing simulated-delay arm unchanged.
  The simulated backend delay is structurally impossible in CLANGD
  mode: session.c applies it only when mode == ENCA_LSP_LOOPBACK.
- New `enca-evs-lsp-sync TEXT`: pushes document state to the real
  server.  First call = didOpen, subsequent calls = didChange (full),
  always with version == current ENCA revision (invariant: LSP
  textDocument.version maps to ENCA revision).  No-op (nil) when no
  real server is armed.  Harness discipline: never sync while a
  completion request is in flight (the blocking complete makes this
  natural).
- URI fixed: file:///enca-evs-live.c.  didOpen makes it an in-memory
  buffer server-side; nothing on disk is touched.
- Position mapping: completion requests go out as line=0,
  character=cursor byte offset.  Valid because harness documents are
  SINGLE-LINE ASCII (byte == UTF-8 == UTF-16 code units).  Prefix
  fidelity to clangd's textual token is intentionally loose -- this
  phase measures candidate-ready latency through the real user path,
  not semantic precision of candidates (same discipline as 5.3/5.3.1,
  where the prefix traveled as a parameter, not as buffer edits).

## 3. Cells (real clangd; same shapes as CACHE.md section 11)

| cell | content | expected source |
|---|---|---|
| COLD | first completion after start, unprimed | miss (rides clangd parse/index burst; n=1, reported, never hidden) |
| RETRY | prime once, N identical requests | hit-exact |
| GROWTH | vocabulary words char-by-char at one anchor (section 9.3 workload) | exact + extend + miss |
| NOVEL | fresh pseudo-random prefixes, constant revision | miss (REAL backend round trips) |
| EDITMIX | bump-revision + lsp-sync + completion each iteration | miss by design |

## 4. Gates

- false_hit == 0 remains ABSOLUTE.  No new cache logic lands here, so
  any non-hit source on RETRY or any hit on EDITMIX is a harness bug.
- Structural expectations carried over from 5.3.1: GROWTH must
  reproduce the F1 engine-side mix exactly (exact=9 / extend=2 /
  miss=21 => avoided 34.4%); NOVEL/EDITMIX all miss.
- No threshold on the clangd MISS latency itself (section 4: threshold
  decided by experiment); it is REPORTED and compared against the B2
  C-level figure (77ms) and the injected 90ms arm.
- Engine-side hit class stays <1ms (C8c engine clause); visible floor
  attribution reported separately per EVS-4.4 discipline.

Output lines (same clock semantics as KPV31/SUM31):
`KPV54|cell|eng_ms|ui_ms|inst_ms|red_ms|total_ms|src` per op and
`SUM54|cell|ops=|exact=|extend=|miss=|avoided=%|eng_p50=|vis_p50=|vis_p95=|vis_p99=|vis_p99.9=|vis_max=|inst_p50=|red_p50=`

## 5. Environment

WSL Ubuntu; clangd installed via apt => Ubuntu clangd 18.1.3 at
/usr/bin/clangd (enca_lsp_find_clangd finds it; harness passes the
path explicitly anyway).  The Windows-side 19.1.0 binary is NOT used:
the emacs build runs in WSL and cross-boundary pipes were never
verified.  Version recorded here because candidate-ready latency is
clangd-version-dependent.

## 6. Outcome -- real LSP through the elisp user path (2026-08-25)

Raw: bench/results/evs54_real_lsp.log.  Harness:
test/enca/evs54-real-lsp.el; runner: bench/enca/evs54_run.sh.
Final numbers below come from the FIXED build (see 6.1); the binary
was rebuilt and re-run after the two defects were repaired so the
recorded data provably corresponds to the committed code.

| cell | ops | exact | extend | miss | avoided | eng p50 | vis p50 | vis p95 | vis max |
|---|---|---|---|---|---|---|---|---|---|
| COLD | 1 | 0 | 0 | 1 | -- | **3.51ms** | 10.5ms | -- | 10.5ms |
| RETRY | 30 | 30 | 0 | 0 | **100%** | **0.14ms** | 2.6ms | 13.0 | 13.5 |
| GROWTH | 32 | 9 | 2 | 21 | **34.4%** | 4.29ms | 7.0ms | 12.0 | 14.7 |
| NOVEL | 15 | 0 | 0 | 15 | 0% | **4.60ms** | 7.4ms | 95.9 | 95.9 |
| EDITMIX | 12 | 0 | 0 | 12 | 0% (by design) | 4.27ms | 6.9ms | 26.3 | 26.3 |

Hit-class attribution: install p50 ~0.06-0.09ms, redisplay p50
~2.3-2.5ms -- same tty-paint-floor picture as 5.3.1.

Gates:
- false_hit == 0 holds; structural expectations intact (RETRY all
  hit-exact, EDITMIX all miss, GROWTH reproduces the F1 mix exactly:
  exact=9 / extend=2 / miss=21 => avoided 34.4%).
- Engine-level SOURCE SEQUENCE is identical op-by-op to the 5.3.1
  loopback run (32/32 GROWTH positions match hit/miss placement):
  swapping the fake backend for a real server is transparent to the
  cache layer.  Cross-validation stronger than ever.
- C8c engine clause MET per class (<1ms on hits).

Reading (honest caveats):
- NOVEL real-miss p50 ~4.6ms is MUCH faster than both the injected
  90ms arm and the B2 real-project figure (~77ms): the synthetic
  single-line no-include document gives clangd a trivially small AST,
  and didOpen lets parsing overlap setup.  This phase validates PATH
  correctness and per-class shape; it does NOT revise backend
  dominance.  For realistic projects the 78-92ms candidate-ready band
  (REPORT section 21) remains the operative number.
- One NOVEL outlier (vis 95.9ms p95 tail) shows clangd occasionally
  stalling well past its median even on the toy document -- consistent
  with periodic index/status work; reported as measured.

## 6.1 Two real defects found & fixed by this phase

1. read_frame frame-boundary loss (src/enca/lsp/session.c).  After
   consuming ONE LSP frame the accumulator was reset to empty,
   DISCARDING any pipelined bytes already pulled from the pipe.  The
   loopback shuffler echoes strictly one-frame-per-write so the flaw
   was invisible for every loopback phase; a real server freely
   pipelines publishDiagnostics/$/progress frames with replies, which
   desynchronized parsing (garbage payloads -> wild label pointers ->
   worker SIGSEGV).  Fix: carry the remainder (memmove) instead of
   resetting.

2. Missing prototypes in src/enca-evs.c (latent since cacc4a55,
   EVS-5.2.6): enca_malloc/enca_free/enca_json_collect_item_labels
   were called without any visible declaration => C99 implicit-int
   rules.  Undefined behavior with compiler-dependent codegen: at -O2
   GCC happened to pass the full RAX through (verified: a binary built
   from the pre-fix HEAD re-runs the 531 harness cleanly and
   reproduces the canonical numbers), but nothing guarantees that, and
   an -O0 build materializes the int conversion (sign-extend EAX),
   deterministically corrupting every heap result -- exactly the crash
   this phase initially hit on the first cache-miss allocation.
   Fixed by including enca/memory/memory.h and enca/lsp/jsonrpc.h;
   also added missing sys/wait.h in session.c (waitpid).

While there: fixed an else-branch brace bug in evs_ct_exec that
counted ct_misses even on cache hits (statistics only; no gate
consumed those counters).

Regression state after fixes: native suite 34201 checks / 0 failures
(with the B2 clangd arms now genuinely executing under WSL clangd
18.1.3), plus the full evs54 run above.
