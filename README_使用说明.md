# 通达信 CON_月多：Java 对齐版 DLL

## 1. 修复目标

这版对照 bebopze/tdx-quant 仓库中的 TdxFun.java 实现计算对齐：

- MACD：严格按 TdxFun.MACD(close) 默认参数 (12, 26, 9)，分别调用 EMA(close,12)、EMA(close,26)，计算 DIF 原始值，再用 EMA(DIF,9) 计算 DEA，MACD=(DIF-DEA)*2。DIF/DEA/MACD 只在返回序列时按 rdArray(...,5) 规则保留 5 位小数。
- EMA：按 TdxFun.EMA(double[],N) 实现，从第一个 finite 样本初始化，并按 Java 的 finite 规则递推。
- SAR：按 TdxFun.TDX_SAR(high,low,2,20) 的状态机实现，包括初始多头状态、EP/AF 更新、SAR 修正、反转条件及反转后的 SAR 赋值。
- 日线、周线、月线 MACD 都由 DLL 内部同一套 CalcMacd 实现计算；日线公式不再混用通达信内置 MACD 与 Java MACD。
- 周 SAR 对截至当天的数据聚合周 HIGH/LOW，再调用同一 CalcTdxSar(...,2,20) 算法。

## 2. 编译产物

- 64 位：dist\x64\TDXMonthBull_x64.dll
- 32 位：dist\x86\TDXMonthBull_x86.dll

使用 Windows GitHub Actions workflow 自动构建。成功后从 Actions artifact 下载最新 DLL。

## 3. 内部函数编号与公式

假设绑定到通达信第 1 号 DLL：

| 内部函数编号 | 调用形式 | 说明 |
|---:|---|---|
| 1 | TDXDLL1(1,C,MONTH_START,MODE) | 月线动态 MACD；MODE 0=比率，1=接近金叉，2=月金叉，3/4/5=MACD/DIF/DEA |
| 2 | TDXDLL1(2,C,WEEK_START,0) | 周线动态 MACD 是否大于等于零轴 |
| 3 | TDXDLL1(3,H,L,WEEK_START) | 周线动态 SAR，按 TDX_SAR(2,20) |
| 4 | TDXDLL1(4,C,MODE,0) | 日线 Java MACD；MODE 0=DIF，1=DEA，2=MACD |

若 DLL 绑定到其他槽位，把公式中的 TDXDLL1 改为对应的 TDXDLLn。

## 4. 编译与安装

需要 Visual Studio 2022 或 C++ Build Tools 以及 MSVC x86/x64 工具。

- x64：打开“x64 Native Tools Command Prompt for VS 2022”，运行 build_x64.bat。
- x86：打开“x86 Native Tools Command Prompt for VS 2022”，运行 build_x86.bat。

32 位通达信只能绑定 x86 DLL；64 位通达信只能绑定 x64 DLL。

## 5. 必须知道的精度边界

算法按 Java 实现翻译，但通达信 DLL 的系列接口使用 float* 输入/输出，而 Java TdxFun 使用 double[]。因此所有算法步骤、EMA 初始化、MACD 参数和 SAR 状态机按 Java 对齐，但 DLL ABI 的单精度输入/输出意味着无法承诺每个中间数值与 Java double 结果 bitwise 完全一致。常规行情下应通过逐日输出比对验证差异。

## 6. 历史数据无未来函数验证

DLL 按日线顺序动态聚合周/月K，计算每一天时只使用截至该日的日线输入。仍应在真实通达信客户端测试 DLL 加载、公式语法、日期边界和逐日输出。
