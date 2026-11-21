#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""对比三方结果：Java 参考实现 / C++ DLL 公式层 / ext_data(ground truth)"""
import sys
import csv


def load(path, col):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    return rows


def main():
    java_path, cpp_path, label = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ""
    j = load(java_path, 'java')
    c = load(cpp_path, 'calc')
    n = len(j)
    assert len(c) == n, (len(j), len(c))
    jm = sum(1 for r in j if r['java'] != r['expected'])
    cm = sum(1 for r in c if r['calc'] != r['expected'])
    jc = 0
    diffs = []
    for i in range(n):
        a = j[i]['java']
        b = c[i]['calc']
        e = c[i]['expected']
        if a != b:
            jc += 1
            diffs.append((i, j[i]['date'], a, b, e))
    print(f"{label} n={n}  java!=ext:{jm}  cpp!=ext:{cm}  java!=cpp:{jc}")
    for d in diffs[:15]:
        print("   diff idx=%d date=%s java=%s cpp=%s ext=%s" % d)
    if len(diffs) > 15:
        print(f"   ... {len(diffs)-15} more")


if __name__ == '__main__':
    main()
