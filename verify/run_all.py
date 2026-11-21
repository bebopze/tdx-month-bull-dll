#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批量三方对比：Java 参考 vs C++ DLL 公式层 vs ext_data(ground truth)

对每个序列跑三种 C++ 变体：
  new      : --key=date                      （修复后推荐接口：DLL 依据交易日历切周/月）
  old      : --key=flags --week=formula      （旧版公式 WEEK_START，用于复现历史 bug）
  flagiso  : --key=flags --week=iso          （旧接口 + 正确周边界，验证兼容分支）
"""
import csv
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out_tmp")


def run_java(src, dst):
    subprocess.run(["java", "-cp", os.path.join(HERE, "java", "out"), "ref.Main", src, dst],
                   check=True, capture_output=True)


def run_cpp(src, dst, extra):
    subprocess.run([os.path.join(HERE, "harness"), src, dst] + extra, check=True, capture_output=True)


def cmpcsv(a_path, b_path, col_a, col_b):
    ja = list(csv.DictReader(open(a_path)))
    cb = list(csv.DictReader(open(b_path)))
    if len(ja) != len(cb):
        raise SystemExit(f"length mismatch {a_path} {b_path}: {len(ja)} vs {len(cb)}")
    return [(int(y['idx']), y['date'], x[col_a], y[col_b]) for x, y in zip(ja, cb) if x[col_a] != y[col_b]]


def main():
    os.makedirs(OUT, exist_ok=True)
    files = sorted(glob.glob(os.path.join(HERE, "data", "*.csv")))
    if not files:
        sys.exit("no data")
    tot_days = tot_ext = tot_new = tot_old = tot_flagiso = 0
    bad_series = []
    for f in files:
        label = os.path.basename(f)[:-4]
        j = os.path.join(OUT, label + ".java.csv")
        c_new = os.path.join(OUT, label + ".new.csv")
        c_old = os.path.join(OUT, label + ".old.csv")
        c_fis = os.path.join(OUT, label + ".flagiso.csv")
        run_java(f, j)
        run_cpp(f, c_new, ["--key=date"])
        run_cpp(f, c_old, ["--key=flags", "--week=formula"])
        run_cpp(f, c_fis, ["--key=flags", "--week=iso"])
        d_ext = cmpcsv(j, c_new, 'java', 'expected')   # Java 与 ext_data 的一致性（校验参考实现）
        d_new = cmpcsv(c_new, j, 'calc', 'java')
        d_old = cmpcsv(c_old, j, 'calc', 'java')
        d_fis = cmpcsv(c_fis, j, 'calc', 'java')
        n = sum(1 for _ in csv.DictReader(open(f)))
        tot_days += n
        tot_ext += len(d_ext)
        tot_new += len(d_new)
        tot_old += len(d_old)
        tot_flagiso += len(d_fis)
        if d_ext or d_new or d_fis:
            bad_series.append(label)
        print(f"{label:26s} n={n:5d}  java_ext={len(d_ext):3d}  new={len(d_new):3d}  old={len(d_old):4d}  flagiso={len(d_fis):3d}")
        for d in (d_ext + d_new + d_fis)[:5]:
            print(f"      !! idx={d[0]} date={d[1]} A={d[2]} B={d[3]}")

    print("\n==== TOTAL ====")
    print(f"series={len(files)}  days={tot_days}")
    print(f"java_vs_ext      = {tot_ext}")
    print(f"new(date-key)    = {tot_new}")
    print(f"old(flags+formula)= {tot_old}")
    print(f"flags+iso        = {tot_flagiso}")
    if bad_series:
        print("unexpected series:", bad_series)


if __name__ == '__main__':
    main()
