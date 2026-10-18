# 通达信 CON_月多：DLL 动态周/月聚合修复

本项目根据用户提供的 Java 参考实现，按日线时间顺序动态聚合周/月 K，目标是消除日线公式读取尚未结束的周/月指标值所造成的前视。

## 构建

需要 Visual Studio 2022 或 C++ Build Tools，并安装 MSVC x86/x64 工具。

- 64 位：在 **x64 Native Tools Command Prompt for VS 2022** 中运行 `build_x64.bat`，输出 `dist\x64\TDXMonthBull_x64.dll`。
- 32 位：在 **x86 Native Tools Command Prompt for VS 2022** 中运行 `build_x86.bat`，输出 `dist\x86\TDXMonthBull_x86.dll`。

32 位通达信只能绑定 x86 DLL，64 位通达信只能绑定 x64 DLL。

## DLL 函数

| 内部函数标记 | 调用形式（假设绑定第 1 号 DLL） | 用途 |
|---:|---|---|
| 1 | `TDXDLL1(1,C,MONTH_START,MODE)` | MODE 0 月 DIF/DEA 比率，MODE 1 月接近金叉，MODE 2 月金叉 |
| 2 | `TDXDLL1(2,C,WEEK_START,0)` | 动态周 MACD 是否大于等于 0 |
| 3 | `TDXDLL1(3,H,L,WEEK_START)` | 动态周 SAR |

如果绑定到其他 DLL 槽位，将公式中的 `TDXDLL1` 改为相应 `TDXDLLn`。DLL 内部函数标记和 DLL 槽位号是不同概念。

## 自动构建

`.github/workflows/build-windows-dll.yml` 使用 Windows runner 构建 x86/x64 版本，并提供名为 `TDXMonthBull-x86-x64` 的短期构建 artifact。进入仓库的 Actions 页面，选择最近一次成功构建，在 Artifacts 下载压缩包。

## 验证须知

源码目标是因果动态聚合，但还需要在真实通达信客户端验证 DLL ABI、公式语法及逐日信号。公式文件保留了用户原有的 `均线预萌出` 引用与 `CON_RPS := 1` 占位配置；这两项需要按实际指标定义核验。Java 的月线样本计算语义与原公式 `REF(DIF_M,1*20)`/日线展开 `BARSLASTCOUNT` 可能不一致，不能承诺所有历史信号与旧公式完全相同。
