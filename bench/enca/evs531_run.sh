#!/bin/bash
# EVS-5.3.1 runner (C13/C8c full version): sync+build, fake backend,
# tty emacs, dump results.  Mirrors evs52_run.sh.
set -e
bash /mnt/c/Users/14977/source/repos/emacs/bench/enca/evs23_sync_build.sh

cd ~/enca-p11
export TERM=xterm-256color
export EVS_BACKEND_DELAY_MS="${EVS_BACKEND_DELAY_MS:-90}"
export EVS52_SERVER="/usr/bin/python3 /mnt/c/Users/14977/source/repos/emacs/test/enca/fake_lsp.py"
export EVS53_LOG=/tmp/evs531_out.txt
rm -f "$EVS53_LOG"
timeout 300 script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs531-ui-typing.el" /dev/null > /dev/null 2>&1 || true
echo "--- RESULTS ---"
cat "$EVS53_LOG" 2>/dev/null || echo NO_LOG
