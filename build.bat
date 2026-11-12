@echo off
rem Builds GenTools.ofx.bundle (Alpha_Gen and Clean_Gen) and the headless selftest with MSVC (x64).
setlocal
set ROOT=%~dp0

where cl.exe >nul 2>&1
if %ERRORLEVEL%==0 goto compiler_ready

if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto compiler_ready
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto compiler_ready
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto compiler_ready
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto compiler_ready
)

echo Error: Visual Studio 2022 C++ compiler (vcvars64.bat) not found.
exit /b 1

:compiler_ready
set OUT=%ROOT%build\GenTools.ofx.bundle\Contents\Win64
if /I "%1"=="staging" set OUT=%ROOT%staging\GenTools.ofx.bundle\Contents\Win64
set OBJ=%ROOT%build\obj
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

rem Copy icons to bundle Resources directory
set RES=%ROOT%build\GenTools.ofx.bundle\Contents\Resources
if not exist "%RES%" mkdir "%RES%"
if exist "%ROOT%icons\*.png" copy /y "%ROOT%icons\*.png" "%RES%\" >nul

set CFLAGS=/nologo /std:c++17 /O2 /EHsc /MT /W3 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%vendor\openfx\include" /Fo"%OBJ%\\"
set LIBS=winhttp.lib windowscodecs.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib shlwapi.lib
set COMMON="%ROOT%src\comfy.cpp" "%ROOT%src\media.cpp" "%ROOT%src\engine.cpp"

if /I "%1"=="selftest" goto selftest
cl %CFLAGS% /LD "%ROOT%src\AlphaGenSAM.cpp" %COMMON% /Fe"%OUT%\GenTools.ofx" /link %LIBS% opengl32.lib user32.lib shell32.lib gdi32.lib || exit /b 1
del "%OUT%\GenTools.exp" "%OUT%\GenTools.lib" 2>nul

:selftest
cl %CFLAGS% "%ROOT%src\selftest.cpp" %COMMON% /Fe"%ROOT%build\agsam_selftest.exe" /link %LIBS% || exit /b 1
echo Built %OUT%\GenTools.ofx and build\agsam_selftest.exe
