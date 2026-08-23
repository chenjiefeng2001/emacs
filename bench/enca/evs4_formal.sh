#!/bin/bash
# EVS-4 formal evidence: 100MB extraction budget cell + LSP-latency storm.
cd /mnt/c/Users/14977/source/repos/emacs/test/enca
make enca_tests > /dev/null 2>&1 || { echo BUILD_FAIL; exit 1; }
cp enca_tests /tmp/ct_formal.exe
echo "=== budget sweep incl 100MB ==="
ENCA_CT_SWEEP=2 /tmp/ct_formal.exe 2>&1 | grep -E "CTBUDGET|checks,"
echo "=== storm with 20ms simulated backend latency ==="
ENCA_CT_STORM_DELAY_US=20000 /tmp/ct_formal.exe 2>&1 | grep -E "CTSTORM|checks,"
echo "=== storm with 5ms backend latency ==="
ENCA_CT_STORM_DELAY_US=5000 /tmp/ct_formal.exe 2>&1 | grep -E "CTSTORM|checks,"
