#!/bin/bash
# EVS-5.5 runner: real editing trace against REAL clangd.
# Contract: bench/enca/evs5/TRACE.md.  Measurement only; no cache
# changes, so no rebuild is required beyond the usual sync check.
set -e
test -x /usr/bin/clangd || { echo "NO_CLANGD"; exit 1; }
clangd --version | head -1
cd ~/enca-p11
export TERM=xterm-256color
export EVS53_LOG=/tmp/evs55_out.txt
rm -f "$EVS53_LOG"
timeout 300 script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs55-trace.el" /dev/null > /dev/null 2>&1 || true
echo "--- RESULTS ---"
cat "$EVS53_LOG" 2>/dev/null || echo NO_LOG
