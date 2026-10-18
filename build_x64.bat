@echo off
setlocal
REM Must run in "x64 Native Tools Command Prompt for VS 2022".
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo ERROR: Current MSVC target is "%VSCMD_ARG_TGT_ARCH%".
  echo Open "x64 Native Tools Command Prompt for VS 2022" and rerun build_x64.bat.
  exit /b 2
)
if not exist dist\x64 mkdir dist\x64
cl /nologo /std:c++17 /EHsc /O2 /MT /LD TDXMonthBull.cpp /link /DEF:TDXMonthBull_x64.def /MACHINE:X64 /OUT:dist\x64\TDXMonthBull_x64.dll
if errorlevel 1 exit /b 1
echo.
echo SUCCESS: dist\x64\TDXMonthBull_x64.dll [64-bit / x64]
endlocal
