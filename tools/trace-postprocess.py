#!/usr/bin/env python3 -u
#
# Can be used to convert an atari800 trace file into a frequency map,
# sorted by the most frequent ranges

import re
import sys
from functools import cmp_to_key

RANGE = 65536
counts = [0] * RANGE

# Read all lines from stdin

print("Reading trace")
lines = sys.stdin.readlines()
for l in lines:
    m = re.match(".*PC=(....): (..) (..) (..).*", l)
    assert m, f"Couldn't match line: {l}"
    pc = int(m[1], 16)
    op0 = m[2].strip()
    op1 = m[3].strip()
    op2 = m[4].strip()
    counts[pc] += 1
    if op1:
        counts[pc + 1] += 1
    if op2:
        counts[pc + 2] += 1


pcfreqs = [[i, counts[i]] for i in range(0, RANGE) if counts[i] > 0]

print("Sorting")

# Sort by frequencies asc, and then by PC asc

def comparator(x, y):
    if x[1] < y[1]:
        return -1
    if x[1] == y[1]:
        return x[0] - y[0]
    return 1

pcfreqs_sorted = sorted(pcfreqs, key=cmp_to_key(comparator))

print("Result")

print("  freq  from .. to")
prev_start = 0
prev_end = 0
prev_count = 0

def emit():
    print('%6d  %04x .. %04x' % (prev_count, prev_start, prev_end))

for r in pcfreqs_sorted:
    pc = r[0]
#    print(hex(pc))
    count = r[1]
    if prev_count != count or pc != prev_end + 1:
        if prev_count > 0:
            emit()
        prev_count = count
        prev_start = pc
    prev_end = pc
emit()
