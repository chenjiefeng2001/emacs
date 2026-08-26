# EIPB Phase 4.2 -- SOAK-2H mixed-load drift (T4)
# Executed 2026-08-26.  Raw: bench/results/eipb_t42.log
# (+ sidecar bench/results/eipb_t42_rss.csv, 60 s samples)
#
# Identical harness/workload/sampling/TA-buckets as Phase 4.1
# (zero harness changes, zero runtime changes).  One continuous
# 2-hour session per build (A/B/C/D), shuffled order [ORDER],
# SECS=7200, ten-minute TA buckets per frozen contract.
#
# Purpose (set by T4.1): the build-independent typing-median drift
# (~4x over 30 min in ALL builds incl ENCA-disabled B) showed NO
# saturation within its observation window.  This run decides:
#   saturating curve  -> warm-up/steady-state property
#   unbounded growth  -> long-session degradation candidate
# plus the four frozen readings: type-c p50 shape, tail sync,
# A/B/C/D co-trajectory, RSS/GC account correlation.

## 0. Verdict

TODO-FINAL

## 1. Typing drift curve -- saturation question

TODO-FINAL (analyzer section "TYPING DRIFT CURVE": per-window
series + first-third vs last-third slope + plateau estimate)

## 2. Tail amplification over 2h (first/last 10-min buckets)

TODO-FINAL

## 3. Co-trajectory check (A vs B/C/D)

TODO-FINAL

## 4. RSS / GC accounts vs latency correlation

TODO-FINAL

## 5. Exit/teardown + integrity

TODO-FINAL

## 6. Reading discipline

- Observation layer only; no causal language without doctrine-8
 要件 (same workload + controlled machine + randomized order +
 N sessions + effect above noise band).
- T4.1 C=2.15 stays UNRESOLVABLE; this run's session-position
 evidence may bear on it but cannot be patched into a pass.
- A flat ~90ms plateau would NOT license "known Emacs issue"
 language; it licenses "steady-state reached, magnitude mapped".
