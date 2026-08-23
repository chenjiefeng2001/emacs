#!/bin/bash
# EVS-4.4: run the UI attribution harness in a REAL terminal emacs
# (pty via script) so forced redisplay actually paints.
cd ~/enca-p11 || exit 1
export TERM=xterm-256color
script -qec "./src/emacs -nw -Q --no-site-file --eval '(load \"/mnt/c/Users/14977/source/repos/emacs/test/enca/evs44-ui.el\")'" /dev/null 2>&1
echo "RC=$?"
