#!/bin/bash
# EVS-5.2.6 runner: sync+build, fake backend, tty emacs, dump results.
set -e
bash /tmp/sb.sh                      # sync + build (script prepared)
cat > /tmp/fake_lsp.sh <<'SH'
#!/bin/bash
export FAKE_DELAY_MS="${FAKE_DELAY_MS:-90}"
export FAKE_ITEMS=50
exec python3 /mnt/c/Users/14977/source/repos/emacs/test/enca/fake_lsp.py
SH
chmod +x /tmp/fake_lsp.sh

cd ~/enca-p11
export TERM=xterm-256color
export EVS52_SERVER="/usr/bin/python3 /mnt/c/Users/14977/source/repos/emacs/test/enca/fake_lsp.py"
export EVS52_LOG=/tmp/evs52_out.txt
rm -f "$EVS52_LOG"
timeout 300 script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs52-ui.el" /dev/null > /dev/null 2>&1 || true
echo "--- RESULTS ---"
cat "$EVS52_LOG" 2>/dev/null || echo NO_LOG
