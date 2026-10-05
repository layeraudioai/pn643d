@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"

rem Install devkitPro first with its official installer. This script then installs
rem the SDK/build packages and builds the repository's bundled 3DS libraries.
if "%DEVKITPRO%"=="" set "DEVKITPRO=C:\devkitPro"
if not exist "%DEVKITPRO%\msys2\usr\bin\bash.exe" (
  echo ERROR: devkitPro MSYS2 was not found at "%DEVKITPRO%\msys2". 1>&2
  echo Install devkitPro/devkitARM, then reopen this command prompt and rerun this file. 1>&2
  exit /b 1
)
if "%DEVKITARM%"=="" set "DEVKITARM=%DEVKITPRO%\devkitARM"
set "PATH=%DEVKITPRO%\msys2\usr\bin;%DEVKITPRO%\devkitARM\bin;%DEVKITPRO%\tools\bin;%PATH%"
set "BASH=%DEVKITPRO%\msys2\usr\bin\bash.exe"

"%BASH%" -lc "pacman -Syu --noconfirm && pacman -S --needed --noconfirm devkitARM libctru citro3d 3dstools cmake make git mingw-w64-x86_64-gcc"
if errorlevel 1 (
  echo ERROR: devkitPro package installation failed. 1>&2
  exit /b 1
)

if not exist "%ROOT%\romconvert.exe" (
  echo ERROR: romconvert.exe is missing. Compile it from Tools\romconvert_aio.cpp with a C++17 compiler, then rerun this setup. 1>&2
  echo Example: g++ -std=c++17 -O2 -Wall -Wextra -o romconvert.exe Tools\romconvert_aio.cpp 1>&2
  exit /b 1
)

"%ROOT%\romconvert.exe" --root "%ROOT%" setup
if errorlevel 1 exit /b 1

echo.
echo Dependencies installed. Use romconvert.exe build-3ds to build DaedalusX64.
echo Use romconvert.exe build-n64recomp only if you also need the optional N64Recomp CLI.
exit /b 0
