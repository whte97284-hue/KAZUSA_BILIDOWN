@echo off
setlocal
echo [1/2] Init MSVC 2022 x64 env...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] VS BuildTools not found!
    exit /b 1
)

echo [2/2] Building RobustTest (A1/A2/B1)...
cd /d "%~dp0"
cl.exe /utf-8 /std:c++17 /EHsc /O2 /W3 /Fe:RobustTest.exe test_robust.cpp BiliDownloader.cpp BiliParser.cpp BiliHttpClient.cpp BiliSigner.cpp /link winhttp.lib advapi32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [OK] Built: cpp_core\RobustTest.exe
    echo.
) else (
    echo [BUILD FAILED]
)
