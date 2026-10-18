@echo off
setlocal
REM Must run in "x86 Native Tools Command Prompt for VS 2022".
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo ERROR: Current MSVC target is "%VSCMD_ARG_TGT_ARCH%".
  echo Open "x86 Native Tools Command Prompt for VS 2022" and rerun build_x86.bat.
  exit /b 2
)
if not exist dist\x86 mkdir dist\x86
cl /nologo /std:c++17 /EHsc /O2 /MT /LD TDXMonthBull.cpp /link /DEF:TDXMonthBull_x86.def /MACHINE:X86 /OUT:dist\x86\TDXMonthBull_x86.dll
if errorlevel 1 exit /b 1
echo.
echo SUCCESS: dist\x86\TDXMonthBull_x86.dll [32-bit / x86]
endlocal
