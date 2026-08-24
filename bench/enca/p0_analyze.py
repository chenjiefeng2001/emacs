#!/usr/bin/env python3
import statistics as st
from collections import defaultdict

def load(path, skip1=True):
    data = defaultdict(lambda: defaultdict(list))
    for i, line in enumerate(open(path)):
        line = line.strip()
        if not line or (skip1 and i == 0):
            continue
        parts = line.split(',')
        if len(parts) < 4:
            continue
        build, suite = parts[0], parts[1]
        rest = ','.join(parts[3:])          # cell|rep|ms (tty uses '|')
        bits = rest.split('|')
        bits = rest.split('|')
        if len(bits) >= 3:
            cell, ms = bits[0], bits[-1]
        elif len(parts) >= 6:
            cell, ms = parts[3], parts[5]
        else:
            continue
        try:
            v = float(ms)
        except ValueError:
            continue
        key = f"{suite}/{cell}"
        data[build][key].append(v)
    return data

batch = load('/mnt/c/Users/14977/source/repos/emacs/bench/results/p0_baseline.csv')
tty = load('/mnt/c/Users/14977/source/repos/emacs/bench/results/p0_baseline_tty.csv', skip1=False)

def show(data, title):
    print(f"== {title} ==")
    cells = sorted({c for b in data for c in data[b]})
    hdr = "%-26s %10s %10s %10s %7s %7s" % ("cell","A_med","B_med","C_med","B/A","C/B")
    print(hdr)
    for c in cells:
        m = {}
        for b in "abc":
            v = data[b].get(c)
            m[b] = st.median(v) if v else float('nan')
        rba = m['b']/m['a'] if m['a'] else float('nan')
        rcb = m['c']/m['b'] if m['b'] else float('nan')
        print("%-26s %10.3f %10.3f %10.3f %7.3f %7.3f" %
              (c, m['a'], m['b'], m['c'], rba, rcb))
    print()

show(batch, "BATCH (ms)")
show(tty, "TTY (ms)")
