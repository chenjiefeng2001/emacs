#!/bin/bash
# EVS-5.4 runner: real clangd through the elisp user path.
# Contract: bench/enca/evs5/REAL_LSP.md.  Mirrors evs531_run.sh but
# requires /usr/bin/clangd (no fake backend, no injected delay).
set -e
test -x /usr/bin/clangd || { echo "NO_CLANGD"; exit 1; }
clangd --version | head -1
bash /mnt/c/Users/14977/source/repos/emacs/bench/enca/evs23_sync_build.sh

cd ~/enca-p11
export TERM=xterm-256color
export EVS53_LOG=/tmp/evs54_out.txt
rm -f "$EVS53_LOG"
timeout 300 script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs54-real-lsp.el" /dev/null > /dev/null 2>&1 || true
echo "--- RESULTS ---"
cat "$EVS53_LOG" 2>/dev/null || echo NO_LOG
