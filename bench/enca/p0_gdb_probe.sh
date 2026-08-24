#!/bin/bash
cd ~/enca-p11
export EVS_BACKEND_DELAY_MS=90
gdb -batch -ex 'run' -ex 'bt 6' -ex 'info locals' -ex 'x/6i \-24' -ex 'p evs_lsp' -ex 'p \._sifields._sigfault.si_addr' --args ./src/emacs --batch -Q \
    -l /mnt/c/Users/14977/source/repos/emacs/test/enca/probe_min.el \
    2>&1 | grep -aE '^#|SIGSEGV|Segmentation|[0-9]+: |enca|lsp' | head -35
