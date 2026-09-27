@echo off
setlocal enabledelayedexpansion
if not exist roms (
    echo Error: 'roms\' directory not found!
    exit /b 1
)
if not exist daed\rom_locks mkdir daed\rom_locks
del /q daed\rom_locks\* 2>nul

set /a max=32

echo ========================================
echo Starting DaedalusX64 ROM Conversion
echo Source ROMs directory: roms\
echo Maximum !max! parallel jobs
echo =======================================


:top

set /a completed=0
:nextrom
for %%R in (roms\*) do (
    set "rom_file=%%R"
    set "folder_name=%%~nR"
 
    call :romcount
    call :wait_for_slot

    echo [START] Converting: !folder_name! ^(Launched: !completed!/!romcount!^)
    cd daed
    start romconvert.bat "..\!rom_file!" "!folder_name!"
    cd ..
    set /a completed=!completed!+1

)

:wait_for_slot
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)
if !count! GTR !max! (
    goto wait_all_loop
)
:wait_all_loop
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)

if !count! GTR 0 (
    goto wait_for_slot
)

:romcount
set /a count=0
for %%L in (daed\rom_locks\*) do (
    set /a count=!count!+1
)
if !count! GTR 0 (
    call :wait_for_slot
)

set /a romcount=0
for %%R in (roms\*) do (
    set /a romcount=!romcount!+1
)
if %romcount% equ 0 and !count! equ 0 (
    clear
    clear
 )

endlocal