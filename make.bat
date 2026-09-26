@echo off
setlocal enabledelayedexpansion


set /a max=3

echo ========================================
echo Starting DaedalusX64 ROM Conversion
echo Source ROMs directory: roms\
echo Maximum %max% parallel jobs
echo ========================================

if not exist roms (
    echo Error: 'roms\' directory not found!
    exit /b 1
)

if not exist daed\rom_locks mkdir daed\rom_locks
del /q daed\rom_locks\* 2>nul
:top
set /a romcount=0
for %%R in (roms\*) do (
    set /a romcount=!romcount!+1
)

if %romcount% equ 0 (
    echo Error: No ROM files found in 'roms\' directory!
    exit /b 1
)

set /a completed=0

for %%R in (roms\*) do (
    set "rom_file=%%R"
    set "folder_name=%%~nR"

    call :wait_for_slot

    echo [START] Converting: !folder_name! ^(Launched: !completed!/!romcount!^)
    cd daed
    start /B romconvert.bat "..\!rom_file!" "!folder_name!"
    cd ..
    set /a completed=!completed!+1
)

echo ========================================
echo All ROM conversion jobs launched. Waiting for completion...
echo ========================================

:wait_all_loop
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)

if !count! GTR 0 (
    echo [WAITING] !count! background conversion jobs still running...
    goto wait_all_loop
)

echo ========================================
echo All ROM conversions completed successfully!
echo ========================================
pause

endlocal

:wait_for_slot
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)
if !count! GEQ !max! (
    echo [THROTTLE] !count!/!max! jobs running. Waiting for a slot...
    goto wait_for_slot
)