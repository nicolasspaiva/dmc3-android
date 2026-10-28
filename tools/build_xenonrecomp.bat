@echo off
rem Compila o XenonRecomp/XenonAnalyse com clang-cl no ambiente do VS 2022 Build Tools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
cd /d "%~dp0XenonRecomp" || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl || exit /b 1
cmake --build build --target XenonRecomp XenonAnalyse || exit /b 1
