#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 C++ 侧 ISO 周 / 日期键解析：与 Python datetime.isocalendar() 逐项比对。

用法：
  g++ -O2 -std=c++17 -I. -o test_dates test_dates.cpp
  python3 check_dates.py
"""
import datetime
import subprocess
import sys

out = subprocess.run(["./test_dates"], capture_output=True, text=True, check=True).stdout
bad = 0
n = 0
for line in out.splitlines():
    if "|" not in line:
        continue
    left, right = line.split("|")
    ds, wy, wk = left.split()
    y, m, d = map(int, ds.split("-"))
    n += 1
    py = datetime.date(y, m, d).isocalendar()
    if int(wy) != py[0] or int(wk) != py[1]:
        bad += 1
        print(f"ISO MISMATCH  {ds}: cpp=({wy},{wk}) python=({py[0]},{py[1]})")
    seg = right.split("->")[1].strip().split()[0]
    if seg != ds:
        bad += 1
        print(f"DECODE MISMATCH {ds}: got {seg}")

print(f"checked {n} dates, problems: {bad}")
sys.exit(1 if bad else 0)
