@echo off
rem build.bat - Windows build for capsaenc (MSVC, freestanding: no CRT, no STL).
rem Produces build\capsaenc.exe and build\stub.exe, both checked against the size cap.
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "VCVARS=D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [FAIL] vcvars64.bat not found: %VCVARS%
    exit /b 1
)
call "%VCVARS%" >nul 2>&1

if not exist build mkdir build

set "CFLAGS=/nologo /std:c++17 /O1 /GL /GS- /Gy /Gw /utf-8 /EHsc- /GR- /D_CRT_SECURE_NO_WARNINGS /W3"
rem crt_shim.cpp defines memcpy/memset - compiler helpers; /GL cannot be used there.
set "CFLAGS_SHIM=/nologo /std:c++17 /O1 /GS- /Gy /Gw /utf-8 /EHsc- /GR- /D_CRT_SECURE_NO_WARNINGS /W3"
set "LDFLAGS=/SUBSYSTEM:CONSOLE /NODEFAULTLIB /OPT:REF /OPT:ICF /INCREMENTAL:NO /STACK:262144,65536"
set "LIBS=kernel32.lib bcrypt.lib user32.lib"

set "CORE=src\common.cpp src\sha256.cpp src\aes.cpp src\lz4.cpp src\payload.cpp src\io_win.cpp"

echo [1/5] resources
> build\res.rc echo 1 ICON "favicon.ico"
> build\res_ext.rc echo 1 ICON "favicon_ext.ico"
rc /nologo /fo build\res.res build\res.rc || exit /b 1
rc /nologo /fo build\res_ext.res build\res_ext.rc || exit /b 1

echo [2/5] stub
ml64 /nologo /c /Fobuild\chkstk.obj src\chkstk.asm || exit /b 1
cl %CFLAGS_SHIM% /c src\crt_shim.cpp /Fo:build\crt_shim.obj /Fd:build\ || exit /b 1
cl %CFLAGS% src\stub.cpp %CORE% build\crt_shim.obj build\chkstk.obj build\res_ext.res /Fo:build\ /Fd:build\ /Fe:build\stub.exe ^
   /link %LDFLAGS% /ENTRY:sfx_entry %LIBS% || exit /b 1

echo [3/5] embed stub
python tools\embed.py build\stub.exe src\stub_blob.h --name g_stub || exit /b 1

echo [4/5] capsaenc
python tools\embed_src.py src src\src_blob.h || exit /b 1
cl %CFLAGS% src\main.cpp %CORE% build\crt_shim.obj build\chkstk.obj build\res.res /Fo:build\ /Fd:build\ /Fe:build\capsaenc.exe ^
   /link %LDFLAGS% /ENTRY:mainCRTStartup %LIBS% || exit /b 1

echo [5/5] size check
python tools\checksize.py build\capsaenc.exe build\stub.exe --limit 120K || exit /b 1

echo.
echo build OK
echo   build\capsaenc.exe
echo   build\stub.exe
endlocal
