# tdx-month-bull-dll

通达信 `CON_月多` 的无未来函数动态周/月 K 聚合 DLL 项目。

## Files

- `TDXMonthBull.cpp`: DLL implementation and registration IDs 1–3.
- `TDXMonthBull_x86.def` / `TDXMonthBull_x64.def`: exports for 32-bit and 64-bit clients.
- `build_x86.bat` / `build_x64.bat`: build with the matching Visual Studio Native Tools prompt.
- `CON_月多_DLL.txt`: replacement daily formula.
- `.github/workflows/build-windows-dll.yml`: Windows GitHub Actions workflow, including an export-symbol check.

## Get both DLLs

After a successful run, open **Actions → Build TDXMonthBull Windows DLLs** and download the `TDXMonthBull-x86-x64` artifact. Artifacts are temporary; this workflow sets a 14-day retention period.

## Important

Bind x86 DLL only to 32-bit TongDaXin and x64 DLL only to 64-bit TongDaXin. This source has not yet been tested inside the actual TongDaXin desktop client; verify registration, formula compatibility, and day-by-day signals before using it as a production indicator.
