@echo off
setlocal

rem Builds the unsigned developer installer (PadBus-dev-setup.exe) from the
rem driver package in bin\x64\PadBus. Build the driver first with build.cmd or
rem Visual Studio: bin\ is not tracked, so the package has to be produced before
rem this script runs.
rem
rem The sources live in this tracked folder. Compiling happens in
rem dist\installer-build, where the payload is staged next to a copy of
rem payload.rc, so nothing generated ends up beside the sources.
rem
rem The compiler environment is set directly instead of through vcvars64.bat:
rem vcvars needs ProgramFiles(x86) and a clean PATH, which a shell that launches
rem cmd from a POSIX environment does not always provide, and it fails with
rem "the system cannot find the path".
set "MSVC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"

set "PATH=%MSVC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

set "SRC=%~dp0"
for %%i in ("%SRC%..\..") do set "ROOT=%%~fi"
set "PAYLOAD=%ROOT%\bin\x64\PadBus"
set "OUT=%ROOT%\dist\installer-build"

if not exist "%PAYLOAD%\PadBus.sys" (
    echo Build the driver first: "%PAYLOAD%\PadBus.sys" is missing.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%" || exit /b 1

copy /y "%SRC%installer.cpp" "%OUT%\" >nul || exit /b 1
copy /y "%SRC%installer.manifest" "%OUT%\" >nul || exit /b 1
copy /y "%SRC%payload.rc" "%OUT%\" >nul || exit /b 1
copy /y "%PAYLOAD%\PadBus.sys" "%OUT%\PadBus.sys" >nul || exit /b 1
copy /y "%PAYLOAD%\PadBus.inf" "%OUT%\PadBus.inf" >nul || exit /b 1
copy /y "%PAYLOAD%\padbus.cat" "%OUT%\PadBus.cat" >nul || exit /b 1

cd /d "%OUT%" || exit /b 1

rc /nologo /fo payload.res payload.rc
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE installer.cpp payload.res /Fe:PadBus-dev-setup.exe
if errorlevel 1 exit /b 1

rem Embed the requireAdministrator manifest ourselves; the linker's
rem /MANIFESTUAC switch does not survive this toolchain.
mt.exe -nologo -manifest installer.manifest "-outputresource:PadBus-dev-setup.exe;#1"
if errorlevel 1 exit /b 1

echo BUILT %OUT%\PadBus-dev-setup.exe
