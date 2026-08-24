#!/bin/bash
cd ~/enca-p11
export EVS_BACKEND_DELAY_MS=90
gdb -batch \
    -ex 'run' \
    -ex 'bt 4' \
    -ex 'info registers rip rdi rsi rax' \
    -ex 'x/6i $pc' \
    -ex 'p (char*)$rdi' \
    -ex 'p (char*)"/tmp/mark"' \
    --args ./src/emacs --batch -Q \
    -l /mnt/c/Users/14977/source/repos/emacs/test/enca/probe_min.el \
    > /tmp/gfull.txt 2>&1
echo done
