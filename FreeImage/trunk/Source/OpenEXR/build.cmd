@echo off
setlocal EnableDelayedExpansion
set "VSROOT=%~1"
set "ARCH=%~2"
if "%ARCH%"=="Win32" set "ARCH=x86"
set "CMAKE=%VSROOT%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "BUILDDIR=%~dp0%~2\%~3\cmake"
set "INSTALLDIR=%~dp0%~2\%~3\install"
if not exist "%VSROOT%Common7\Tools\VsDevCmd.bat" (
    echo ERROR: Visual Studio environment script not found: "%VSROOT%Common7\Tools\VsDevCmd.bat"
    exit /b 1
)
if not exist "%CMAKE%" (
    echo ERROR: Visual Studio CMake tool not found: "%CMAKE%"
    exit /b 1
)
if not exist "%NINJA%" (
    echo ERROR: Visual Studio Ninja tool not found: "%NINJA%"
    exit /b 1
)
set "VSCMD_DEBUG="
call "%VSROOT%Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
if errorlevel 1 (
    echo ERROR: Visual Studio environment initialization failed for %ARCH%.
    exit /b 1
)
if "%~4"=="clean" (
    if exist "%BUILDDIR%\CMakeCache.txt" "%CMAKE%" --build "%BUILDDIR%" --target clean
    exit /b !errorlevel!
)
"%CMAKE%" -S "%~dp0." -B "%BUILDDIR%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" "-DCMAKE_BUILD_TYPE=%~3" "-DCMAKE_INSTALL_PREFIX=%INSTALLDIR%"
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%BUILDDIR%" --parallel 8
if errorlevel 1 exit /b 1
"%CMAKE%" --install "%BUILDDIR%"
exit /b %errorlevel%