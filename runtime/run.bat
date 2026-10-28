@echo off
rem Regenera os stubs do kernel, compila e roda o DMC3 recompilado (a partir da raiz do projeto).
cd /d "%~dp0.."
call python -I scripts\x360_imports.py x360\DMC3.xex tools\XenonRecomp\XenonUtils\xbox runtime x360\imports.txt
if errorlevel 1 exit /b 1
call recomp\build.bat > recomp\build_log.txt 2>&1
if errorlevel 1 (
    findstr /C:"error" recomp\build_log.txt
    exit /b 1
)
recomp\bin\DMC3.exe x360\DMC3.pe
