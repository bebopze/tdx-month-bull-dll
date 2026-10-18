通达信 DLL 位数选择（请务必匹配）

64 位通达信：绑定 dist\x64\TDXMonthBull_x64.dll
32 位通达信：绑定 dist\x86\TDXMonthBull_x86.dll
不能交叉绑定。

编译 64 位：打开“x64 Native Tools Command Prompt for VS 2022”，运行 build_x64.bat
编译 32 位：打开“x86 Native Tools Command Prompt for VS 2022”，运行 build_x86.bat

DLL 内部函数：
1 = 月线动态 MACD（MODE 0/1/2）
2 = 周线动态 MACD
3 = 周线动态 SAR，严格按 TdxFun.TDX_SAR(HIGH,LOW,2,20) 实现
4 = 日线 MACD，严格按 TdxFun.MACD(C,12,26,9) 实现

注意：算法逻辑按 Java 对齐，但通达信 DLL 系列接口为 float 输入/输出，Java 使用 double；最终请在通达信内逐日比对信号。
