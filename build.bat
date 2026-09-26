@echo off
setlocal
cd /d "%~dp0"
if not exist build\x64 mkdir build\x64
if not exist build\vanilla mkdir build\vanilla

set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"

cmd /c "call %VCVARS% x64 && cl /nologo /std:c++17 /O2 /MT /EHsc /W3 /LD /Isrc src\dllmain.cpp src\proxy.cpp src\log.cpp src\settings.cpp src\inline_hook.cpp src\patches.cpp /Fobuild\x64\ /Febuild\xinput1_3.dll /link /DEF:xinput1_3.def /OPT:REF"
if errorlevel 1 exit /b 1

cmd /c "call %VCVARS% x86 && cl /nologo /std:c++17 /O2 /MT /EHsc /W3 /LD /Isrc src\dllmain.cpp src\proxy.cpp src\log.cpp src\settings.cpp src\inline_hook.cpp src\patches_x86.cpp src\present_x86.cpp /Fobuild\vanilla\ /Febuild\vanilla\xinput1_3.dll /link /DEF:xinput1_3.def /OPT:REF"
if errorlevel 1 exit /b 1

echo Built build\xinput1_3.dll and build\vanilla\xinput1_3.dll
