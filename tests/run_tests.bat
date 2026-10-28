@echo off
rem Gera e roda os testes de semantica das instrucoes acrescentadas ao XenonRecomp.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call python -I gen_insn_tests.py ..\recomp\ppc insn_tests.cpp
if errorlevel 1 exit /b 1
"C:\Program Files\LLVM\bin\clang-cl.exe" /std:c++20 /O1 /EHsc -mavx2 /I..\recomp\ppc /I..\tools\XenonRecomp\thirdparty\simde -w insn_tests.cpp /Fe:insn_tests.exe
if errorlevel 1 exit /b 1
.\insn_tests.exe
