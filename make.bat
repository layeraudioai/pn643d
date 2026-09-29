@echo off
setlocal enabledelayedexpansion

set /a max=5
set /a completed=0

echo ========================================
echo Starting DaedalusX64 ROM Conversion
echo Source ROMs directory: roms\
echo Maximum !max! parallel jobs
echo =======================================


if not exist daed\rom_locks mkdir daed\rom_locks
del /q daed\rom_locks\* 2>nul

if not exist roms (
    echo Error: 'roms\' directory not found!
    exit /b 1
)

mkdir used
set dir=%CD%\daed
mkdir !dir!\cmakedirs
:nextrom
for %%R in (roms\*) do (
    set "rom_file=%%R"
    set "folder_name=%%~nR"
 
    call :romcount
    call :wait_for_slot

    echo [START] Converting: !folder_name! ^(Launched: !completed!/!romcount!^)
    cd "!dir!"
    start romconvert.bat "..\!rom_file!" "!folder_name!"
    set /a completed=!completed!+1
)
call :wait_all_loop
goto end

:wait_for_slot
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)
if !count! GTR !max! (
    cls
    echo waiting for slot: !count!/!max!
    goto wait_for_slot
)
exit /b

:wait_all_loop
set /a count=0
for %%L in (daed\rom_locks\*) do (
    cls
    echo Waiting for all processes to finish: !count!/!romcount!
    set /a count=!count!+1
)

if !count! GTR 0 (
    goto wait_all_loop
)
exit /b

:romcount
set /a romcount=0
for %%R in (roms\*) do (
    set /a romcount=!romcount!+1
)
exit /b

:end
endlocal
