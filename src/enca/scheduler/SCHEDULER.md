# P3 Scheduler -- Semantic Contract

Status: **P3.0 contract frozen; route COMPRESSED by the 2026-08-23
direction calibration (ARCHITECTURE.md "North Star").**

The scheduler is NOT a general-purpose task runtime.  It exists to
serve one concrete vertical: **EVS-1 Emacs Interactive Vertical Slice**
(keypress -> capture -> snapshot -> schedule -> worker -> commit ->
visible effect, measured end to end).  Scope after P3.2 is conditional:
phases below P3.2 exist only if the EVS-1 benchmark shows this minimal
scheduler is the bottleneck.

## 0.1 Compressed route

```
P3.1  Minimal Task + Admission        (unit-tested decisions)
P3.2  Minimal Scheduler               (4 queues + N workers, gates)
EVS-1 Emacs Interactive Vertical Slice + real end-to-end benchmark
      -> bottleneck analysis decides whether any further P3 phase exists
(P3.3+ fairness/scaling/adaptive work: only on EVS-1 evidence)
```

Everything in sections 1-12 remains the semantic contract that P3.1
and P3.2 must obey; sections describing deeper machinery are
aspirational and require re-approval plus EVS-1 evidence.

## 0. What P3 is -- and is not

P3 answers:

> Under many documents x many revisions x many workers x frequent
> cancellation, how does ENCA make its compute land on work that can
> still be committed?

P3 does **not** answer "how do we maximize threads".

Performance targets, in order:

1. **Interactive tail latency** (p50/p90/p95/p99/p99.9 of
   keypress-to-result).
2. **Wasted-work ratio** (executed-but-uncommittable / total executed).

Throughput is a secondary observation, not a goal.

## 1. Terminology

| Term | Definition |
|---|---|
| Work item | Raw bytes/code a worker could run. Has NO identity, NO freshness, NO policy. |
| Task | A *decision record* wrapping a work item with identity, staleness context, urgency, deadline and policy. **Task != work item.** |
| Document | Logical source of revisions (P2 `enca_document`). |
| Revision | Per-document monotonic counter carried by every task. |
| Generation | Runtime-wide epoch (P1). |
| Urgency | Closed five-level enumeration (see section 4). |
| Deadline | Absolute monotonic timestamp after which the result has no value. Independent of urgency. |
| Admission | The decision taken BEFORE a task may enter any queue. |
| Dispatch | Handing an admitted, non-stale task to a worker. |
| Supersession | Formal relation between two tasks in the same domain. |
| Waste | Executed work whose result cannot be committed. |

A task record carries at minimum:

```
Task
 |- task_id              (identity)
 |- document_id          (logical source)
 |- runtime_generation   (epoch at submit)
 |- document_revision    (revision at submit)
 |- snapshot             (retained handle; may be NULL for pure jobs)
 |- urgency              (closed enum)
 |- deadline_ns          (ENCA_DEADLINE_NONE allowed)
 |- cancellation_source  (shared cancellation token)
 |- task_class           (INTERACTIVE/BACKGROUND/MAINTENANCE/SYSTEM)
 '- execution_policy     (opaque key -> AdmissionPolicy lookup)
```

## 2. Business-semantic ignorance (hard boundary)

The scheduler knows ONLY: task class, urgency, deadline, cancellation,
cost hints, resource hints.

It must NEVER contain names or branches for: LSP, completion, parser,
diagnostics, tree-sitter, indexing, Emacs, redisplay.  Future consumers
(P4+) declare `task_class` and policies; they never teach the scheduler
their vocabulary.

## 3. Task classes (closed set)

| Class | Purpose (examples, not obligations) |
|---|---|
| SYSTEM | lifecycle: shutdown barriers, internal reclamation |
| INTERACTIVE | user-visible response paths |
| BACKGROUND | analysis/indexing/precompute |
| MAINTENANCE | self-maintenance: e.g. P2.1 C2 deferred coalescing becomes the first real consumer |

Classes are abstract on purpose: "completion" is a consumer concept;
"interactive" is a scheduling concept.

## 4. Urgency (closed enum) and deadlines are orthogonal

```
enum enca_urgency {
  ENCA_URGENCY_REALTIME     /* never used by default policies */
  ENCA_URGENCY_INTERACTIVE
  ENCA_URGENCY_NORMAL
  ENCA_URGENCY_BACKGROUND
  ENCA_URGENCY_MAINTENANCE
}
```

- No integer priorities.  Magic numbers like "priority 17" are banned.
- Deadlines are independent: a BACKGROUND task may carry a 20 ms
  deadline; an INTERACTIVE task may carry none.

## 5. Lifecycle

```
submit
  |
  v
ADMISSION ------------------ ACCEPT -> queue(class)
  |  ACCEPT / REJECT /         |
  |  COALESCE / REPLACE        v
  |                       DISPATCH gate -- stale/expired? -> DROP
  |  REJECT: caller informed           |
  |  DEFER : parked, retried later     v
  |  COALESCE: folded into an       Execute
  |    equivalent newer task           |
  |  REPLACE: older entry removed      v
  v                                 COMMIT gate (P1 rules)
rejected/dropped
```

Two stale/expiry gates exist BEFORE compute:

- **Admission gate**: at submit time, if an equivalent newer task is
  already admitted, the new submission may REPLACE/COALESCE it.
- **Dispatch gate**: when a worker would take a task, re-validate
  generation, document revision and deadline.  Fail => DROP_STALE /
  DROP_EXPIRED without executing.

Principle: **do not compute what can no longer be committed.**
Worker-side cooperative cancellation remains the last line of defense
(P1 semantics unchanged), not the primary mechanism.

## 6. Supersession contract

Task A is superseded by task B iff:

```
A.document_id  == B.document_id      (same logical work domain)
AND A.task_class == B.task_class     (same kind of work)
AND B.document_revision > A.document_revision   (strictly newer)
```

Consequences:

- Superseded QUEUED tasks leave the queue without executing
  (folded/replaced at admission, or dropped at dispatch).
- An EXECUTING task is never force-removed by supersession alone; it
  observes cancellation cooperatively (P1 rules).
- Cross-document and cross-class tasks NEVER supersede each other.

## 7. Admission decisions

| Decision | Meaning |
|---|---|
| ACCEPT | enters its class queue |
| REJECT | refused synchronously (overload policy), caller informed |
| COALESCE | folded into an existing admitted equivalent task |
| REPLACE | removes an existing admitted equivalent, takes its place |
| DEFER | parked outside queues; re-admitted on a trigger |

AdmissionPolicy is a pluggable table keyed by task_class; v1 ships one
policy per class with supersession-based REPLACE for INTERACTIVE and
BACKGROUND, FIFO ACCEPT for MAINTENANCE, and SYSTEM always ACCEPT.

## 8. v1 scheduling architecture (deliberately boring)

```
scheduler
  |-- INTERACTIVE queue  (highest)
  |-- NORMAL queue
  |-- BACKGROUND queue
  '-- MAINTENANCE queue  (lowest)
        ^
        |  N workers pop strictly by class order, FIFO inside class
```

Explicitly OUT of scope until data demands them: work stealing, NUMA
awareness, CPU affinity, per-core deques, lock-free MPMC, adaptive
migration, preemption.  Known accepted limitation: continuous
INTERACTIVE pressure starves MAINTENANCE; wait-time histograms will
measure it, escalation is deferred.

Fairness contract v1: intra-class ordering is FIFO; inter-class order
is fixed priority; no aging in v1.

## 9. Metrics (required instrumentation)

| Metric | Definition / measurement point |
|---|---|
| schedule latency | submit -> admission decision |
| queue latency | admission -> dispatch |
| execution latency | dispatch -> worker finished |
| end-to-end latency | submit -> commit (or drop) |
| tail latencies | p50 / p90 / p95 / p99 / p99.9 of end-to-end |
| wasted-work ratio | uncommittable executed results / total executed |
| drop accounting | DROP_STALE / DROP_EXPIRED / DROP_SHUTDOWN counters |
| starvation watch | per-class max queue wait time histogram |

Waste is classified at the commit gate using the two-level epoch rule
(ARCHITECTURE.md #19): wrong generation, wrong revision, cancelled, or
expired => waste.

## 10. Shutdown

Shutdown is SYSTEM-class work: cancel every queue (DROP_SHUTDOWN with
per-queue counters), stop dispatch, join workers, let executing tasks
finish or observe cooperative cancellation, run SYSTEM tasks that
remain.  No queued user task survives shutdown.

## 11. Sub-phase route (compressed 2026-08-23 -- see section 0.1)

| Phase | Content | Gate |
|---|---|---|
| P3.1 | task model + admission engine (supersession, expiry), unit tests | admission decisions unit-verified |
| P3.2 | minimal scheduler: 4 queues + N workers + dispatch gates | S1/S2 green; suite green |
| EVS-1 | Emacs interactive vertical slice + end-to-end keypress benchmark | numbers recorded; bottleneck analysis written |
| (conditional) | deeper scheduling work ONLY if EVS-1 names this scheduler as a measured bottleneck | new contract amendment |

Benchmark definitions S1-S7 (S1 single-task cost, S2 burst, S3
interactive burst with revisions, S4 mixed classes, S5 worker-count
scaling, S6 cancellation storm, S7 shutdown storm) are frozen now and
belong to bench/enca/ as P2.1 tooling does.

## 12. Non-goals (until a written amendment)

Work stealing; NUMA; CPU affinity; preemption; adaptive scheduling;
parser-aware or LSP-aware scheduling; thread-pool APIs exposed to
Elisp; GPU.
