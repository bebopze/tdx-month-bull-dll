# tdx-month-bull-dll

通达信 `CON_月多` 的无未来函数动态周/月 K 聚合 DLL 项目。

`月多` 的计算语义严格对齐 [`bebopze/tdx-quant`](https://github.com/bebopze/tdx-quant) 的
`TdxFun.java` / `MonthlyBullSignal2.java`，并已在本地 `tdx` 库的真实 K 线 +
`ext_data[月多]` 上做过逐日回归（249 个标的、474,692 个交易日、0 差异）。
算法细节、修复说明与验证结果见 [`README_使用说明.md`](README_使用说明.md)。

## Files

- `TDXMonthBull.cpp`: DLL 实现（内部函数编号 1~4）。
- `TDXMonthBull_x86.def` / `TDXMonthBull_x64.def`: 32/64 位导出表。
- `build_x86.bat` / `build_x64.bat`: 用对应架构的 Visual Studio Native Tools 提示符编译。
- `CON_月多_DLL.txt`: 配套日线公式（**必须与 DLL 同版本一起更新**）。
- `verify/`: 复现"Java 版 vs DLL 版 月多"逐日对比的测试工具与说明。
- `.github/workflows/build-windows-dll.yml`: Windows CI，含导出符号检查与两架构打包。

## Get both DLLs

推送后打开 **Actions → Build TDXMonthBull Windows DLLs**，下载 `TDXMonthBull-x86-x64` artifact（保留 14 天）。

## Important

Bind x86 DLL only to 32-bit TongDaXin and x64 DLL only to 64-bit TongDaXin.

v2 的公式把**日期键**（`DATE_KEY := DATE`）传给 DLL，由 DLL 按 ISO 周切分周 K；
旧公式（传 `WEEK_START` 标志）仍可运行，但长假后的周边界 bug 依旧存在，请务必换用新公式。
