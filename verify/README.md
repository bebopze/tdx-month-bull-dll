# verify —— 「月多」Java 版 vs DLL(C++) 版 逐日回归工具

目标：用本地 `tdx` 库中的**真实日 K** 和**已算好的 `ext_data[月多]`**，逐日对比
Java 参考实现与 C++（DLL 同一份源码）的 `月多` 结果。

## 组成

| 文件 | 作用 |
|---|---|
| `extract.py` | 从 MySQL(`tdx.base_stock` / `tdx.base_block`) 导出 `kline_his` + `ext_data_his`（zstd 解压、按日期对齐），生成 `data/*.csv` |
| `java/ref/TdxFun.java` | 从 `tdx-quant` 原样拷贝（仅加了一行 `package ref;`） |
| `java/ref/MonthlyBull2Ref.java` | `MonthlyBullSignal2.java` 原样拷贝，仅把外部指标 `均线预萌出` 改为注入（`preBreakInjected`） |
| `java/ref/Main.java` | Java 参考实现驱动，输出逐日 `月多` |
| `java/ref/DumpWeekSar.java` | 诊断：打印指定日期的周 K 与周 SAR 全精度值 |
| `harness.cpp` | `#include "../TDXMonthBull.cpp"`，按 `CON_月多_DLL.txt` 逐行复现公式层，以 float32 调用 DLL 函数并输出逐日 `月多` |
| `run_all.py` | 批量跑三种变体并与 Java 对比，汇总差异 |
| `test_dates.cpp` / `check_dates.py` | 单独校验 DLL 内的日期键解析与 ISO 周（对照 Python `isocalendar`） |

`data/`、`out_tmp/`、`java/out/` 为生成物，已在 `.gitignore` 中忽略。

## 数据格式

```csv
date,open,high,low,close,preBreak,expectedYueDuo
2017-01-03,5.590,5.660,5.570,5.640,0,1
```

- `preBreak`：`ext_data` 中的 `均线预萌出`（外部指标，作为公式输入）
- `expectedYueDuo`：`ext_data` 中的 `月多`（ground truth）

`ext_data` 的字段顺序由 `ExtDataDTO` 决定，0-based 下标：`97 = 月多`、`98 = 均线预萌出`。

## 运行

```bash
# 0) 依赖：mysql 客户端、zstd、python3、JDK、g++
cd verify

# 1) 导出数据（需要本地 tdx 库；可用 TDX_DB_HOST/TDX_DB_PORT/TDX_DB_USER/TDX_DB_PASS 覆盖默认连接）
python3 extract.py --table base_stock --codes 000001,600519,000858 --outdir data
python3 extract.py --table base_block --codes 880904,880471 --outdir data
#   或批量（示例：每行一个代码）
#   cat codes.txt | xargs -P 8 -I{} python3 extract.py --table base_stock --codes {} --outdir data

# 2) 编译 Java 参考实现与 C++ harness
javac -encoding UTF-8 -d java/out java/ref/*.java
g++ -O2 -std=c++17 -o harness harness.cpp

# 3) 批量三方对比
python3 run_all.py

# 4)（可选）单独校验 ISO 周/日期键实现
g++ -O2 -std=c++17 -I. -o test_dates test_dates.cpp
python3 check_dates.py      # -> checked 33 dates, problems: 0
```

`run_all.py` 会为每个序列跑三种 C++ 变体：

| 变体 | 参数 | 含义 |
|---|---|---|
| `new` | `--key=date` | v2 推荐接口：DLL 按 `DATE` 自行切分 ISO 周/自然月 |
| `old` | `--key=flags --week=formula` | v1 行为：公式传 `WEEK_START := WEEKDAY<REF(WEEKDAY,1)`，用于复现历史 bug |
| `flagiso` | `--key=flags --week=iso` | 旧接口 + 正确周边界，验证兼容分支 |

## 当前基线（2026-11-22）

```
series=249  days=474692
java_vs_ext      = 0        # Java 参考实现 == ext_data
new(date-key)    = 0        # v2 DLL == Java
old(flags+formula)= 2383    # v1 DLL 的历史偏差（0.50%）
flags+iso        = 0
```

另外单独验证过：`MODE=3`（DLL 内判定 `MACD_日上0轴`）与公式层用
`DIF/DEA/MACD + HHV(...,10)` 自行判定的结果在 474,692 个交易日上完全一致。

## 说明

- `--hhv=tdx|java`：`HHV(MACD,10)` 在不足 10 根时的取值语义（通达信取已有窗口最大值，Java 版返回 NaN）。
  默认 `java`，与 Java 基准一致；`tdx` 仅用于解释图形最左侧 9 根 K 线的差异。
- `--day0=dll|formula`：日上0轴用 DLL `MODE=3` 还是公式层自行比较（默认 `dll`）。
- `--dump=<idx>`：打印该交易日所属周 K 与周 SAR 的全精度数值，便于定位差异。
