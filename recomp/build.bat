@echo off
rem Compila o codigo recompilado do DMC3 com clang-cl (VS 2022 Build Tools + LLVM).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
cd /d "%~dp0" || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl || exit /b 1
cmake --build build -- -k 0
