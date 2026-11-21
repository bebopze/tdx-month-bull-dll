#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从本地 MySQL(tdx) 抽取真实 K线 + ext_data(月多/均线预萌出) 为对比用 CSV。

输出 CSV 列: date,open,high,low,close,preBreak,expectedYueDuo
  - expectedYueDuo = ext_data_his 中的「月多」(Java 侧 ground truth)
  - preBreak       = ext_data_his 中的「均线预萌出」(外部指标，作为公式输入)

用法:
  python3 extract.py --table base_stock --codes 000001,600519 --outdir data
  python3 extract.py --table base_block --all --limit 50 --outdir data
"""
import argparse
import json
import os
import subprocess
import sys

# 本地开发库连接参数（可用环境变量覆盖；默认值与 tdx-quant 的 shardingsphere-prod.yml 一致）
DB_HOST = os.environ.get("TDX_DB_HOST", "127.0.0.1")
DB_PORT = os.environ.get("TDX_DB_PORT", "3306")
DB_USER = os.environ.get("TDX_DB_USER", "root")
DB_PASS = os.environ.get("TDX_DB_PASS", "tdx@1234")

MYSQL = ["mysql", f"-h{DB_HOST}", f"-P{DB_PORT}", f"-u{DB_USER}", f"-p{DB_PASS}", "-N", "-B", "--raw"]

FIELD_DATE = 0
FIELD_YUEDUO = 97      # ExtDataDTO 字段顺序: 月多
FIELD_PREBREAK = 98    # 均线预萌出


def q(sql):
    p = subprocess.run(MYSQL + ["-e", sql], capture_output=True)
    if p.returncode != 0:
        sys.exit("mysql error: " + p.stderr.decode("utf-8", "replace"))
    return p.stdout


def fetch_blob(table, code, col, rawlen_col):
    out = q(f"select hex({col}), {rawlen_col} from tdx.{table} where code='{code}'")
    line = out.decode().strip().split("\n")[0]
    if not line:
        return None
    parts = line.split("\t")
    if parts[0] == "NULL":
        return None
    rawlen = int(parts[1]) if parts[1] != "NULL" else 0
    hexs = parts[0]
    bin_data = bytes.fromhex(hexs)
    p = subprocess.run(["zstd", "-d", "-q", "-c"], input=bin_data, capture_output=True)
    if p.returncode != 0:
        sys.exit("zstd error")
    data = p.stdout
    if rawlen and len(data) != rawlen:
        print(f"  ! warn {table}/{code}/{col}: decompressed {len(data)} != raw_len {rawlen}", file=sys.stderr)
    return data.decode("utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", default="base_stock")
    ap.add_argument("--codes", default="")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--outdir", default="data")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    if args.codes:
        codes = args.codes.split(",")
    else:
        sql = f"select code from tdx.{args.table}"
        if args.limit:
            sql += f" limit {args.limit}"
        codes = q(sql).decode().split()
    codes = [c.strip() for c in codes if c.strip()]

    manifest = []
    for code in codes:
        try:
            kraw = fetch_blob(args.table, code, "kline_his", "kline_his_raw_len")
            eraw = fetch_blob(args.table, code, "ext_data_his", "ext_data_his_raw_len")
        except SystemExit:
            raise
        except Exception as e:  # noqa
            print(f"  ! skip {code}: {e}", file=sys.stderr)
            continue
        if not kraw or not eraw:
            print(f"  ! skip {code}: empty blob", file=sys.stderr)
            continue
        klines = json.loads(kraw)
        exts = json.loads(eraw)
        ext_by_date = {}
        for e in exts:
            p = e.split(",")
            ext_by_date[p[0]] = p
        rows = []
        for k in klines:
            p = k.split(",")
            d = p[0]
            e = ext_by_date.get(d)
            if e is None or len(e) <= FIELD_PREBREAK:
                continue
            rows.append(",".join([d, p[1], p[2], p[3], p[4], e[FIELD_PREBREAK], e[FIELD_YUEDUO]]))
        if len(rows) < 300:
            print(f"  ! skip {code}: only {len(rows)} aligned rows", file=sys.stderr)
            continue
        name = f"{args.table}_{code}.csv"
        path = os.path.join(args.outdir, name)
        with open(path, "w") as f:
            f.write("date,open,high,low,close,preBreak,expectedYueDuo\n")
            f.write("\n".join(rows) + "\n")
        npos = sum(1 for r in rows if r.split(",")[-1] == "1")
        manifest.append((code, len(rows), npos))
        print(f"  ok {code}: {len(rows)} rows, 月多 positives={npos} -> {path}")

    with open(os.path.join(args.outdir, "manifest.txt"), "w") as f:
        for code, n, npos in manifest:
            f.write(f"{code}\t{n}\t{npos}\n")


if __name__ == "__main__":
    main()
