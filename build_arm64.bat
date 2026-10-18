@echo off
rem Build Windows ARM64 with Visual Studio's x64-hosted ARM64 cross compiler.
rem Usage: build_arm64.bat [Release^|Debug^|RelWithDebInfo^|MinSizeRel]
setlocal
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build\windows-arm64"
set "BUILD_TYPE=Release"
if not "%~1"=="" set "BUILD_TYPE=%~1"
if not "%~2"=="" goto usage
if /i "%BUILD_TYPE%"=="Release" goto config_ok
if /i "%BUILD_TYPE%"=="Debug" goto config_ok
if /i "%BUILD_TYPE%"=="RelWithDebInfo" goto config_ok
if /i "%BUILD_TYPE%"=="MinSizeRel" goto config_ok
goto usage
:config_ok

rem Always select the target environment, even in an existing x64 developer prompt.
rem LuaJIT's msvcbuild.bat switches back to the x64 compiler for its host tools.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_msvc
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto no_msvc
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -no_logo -host_arch=x64 -arch=arm64
if errorlevel 1 goto no_msvc
if /i not "%VSCMD_ARG_TGT_ARCH%"=="arm64" goto no_msvc

where cmake >nul 2>nul
if errorlevel 1 goto no_tools
where ninja >nul 2>nul
if errorlevel 1 goto no_tools
where git >nul 2>nul
if errorlevel 1 goto no_tools
if exist "%ROOT%engine\thirdparty\godot-cpp\CMakeLists.txt" goto have_godotcpp
git -C "%ROOT%." submodule update --init engine/thirdparty/godot-cpp
if errorlevel 1 exit /b 1
:have_godotcpp

rem Explicit system/processor settings keep Ninja and godot-cpp on the ARM64 target.
cmake -S "%ROOT%engine" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=ARM64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --target openbfme openbfme_tests openbfme_peer
if errorlevel 1 exit /b 1

echo BUILD OK: ARM64 GDExtension in godot\bin\; ARM64 tests and peer in build\windows-arm64\
echo Use ARM64 Godot to load the extension. Run ARM64 executables on Windows ARM64.
echo Retail x86 oracle helpers are built separately with tools\retail_oracle\build.bat.
exit /b 0

:usage
echo Usage: build_arm64.bat [Release^|Debug^|RelWithDebInfo^|MinSizeRel]
exit /b 2
:no_msvc
echo ERROR: Visual Studio with C++ x64/x86 tools, ARM64 build tools and a Windows SDK is required.
exit /b 1
:no_tools
echo ERROR: CMake, Ninja and Git must be available on PATH.
exit /b 1
