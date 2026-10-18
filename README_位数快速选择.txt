通达信 DLL 位数选择（请务必匹配）

你使用 64 位通达信：编译并绑定 dist\x64\TDXMonthBull_x64.dll
你使用 32 位通达信：编译并绑定 dist\x86\TDXMonthBull_x86.dll

不能交叉绑定。32 位和 64 位客户端可共用同一份 CON_月多_DLL.txt 公式，但必须分别绑定对应位数的 DLL。

编译 64 位：打开“x64 Native Tools Command Prompt for VS 2022”，运行 build_x64.bat
编译 32 位：打开“x86 Native Tools Command Prompt for VS 2022”，运行 build_x86.bat

当前交付为源码及构建脚本，不包含未经 Windows 编译和通达信实测的 DLL 二进制文件。
