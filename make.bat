@ECHO OFF
setlocal EnableDelayedExpansion

set curdir=%CD%

echo compile and install dependencies
cd picaGL && make && make install && cd ../imgui-picagl && make && make install && cd ../DaedalusX64-3DS

del "Source\SysCTR\Resources\romfs\Roms\*.*64"

FOR %%I IN (..\roms\*.*64) DO (

    del daedbuild\CMakeCache.txt 2>nul

    set "hex=%%~nI"

    rem remove non-hex chars commonly found in names
    for %%C in (
        G H I J K L M N O P Q R S T U V W X Y Z
        g h i j k l m n o p q r s t u v w x y z
        - _ . , ' ! @ # $ %% ^ + = \ / [ ] { }
    ) do (
        set "hex=!hex:%%C=!"
    )
    set hex=!hex: =!
    set "hex=!hex:(=!"
    set "hex=!hex:)=!"
    set "hex=!hex:[=!"
    set "hex=!hex:]=!"
    set "hex=!hex:{=!"
    set "hex=!hex:}=!"
    set "tid=0x!hex:~0,5!"
    echo TID=!tid!
    mkdir "Source\SysCTR\Resources\romfs\Roms\"
    copy "%%I" "Source\SysCTR\Resources\romfs\Roms\" >nul

    (
    for /f "delims=" %%L in (Source\SysCTR\Resources\template2.rsf) do (
      set "line=%%L"
      for %%a in (!tid!) do (
        set "line=!line:0xDAED3=%%a!"
      )
      echo !line!
    )
    ) > Source\SysCTR\Resources\template.rsf
    tools\3dstool -c --romfs-dir "Source\SysCTR\Resources\romfs" --file "Source\SysCTR\Resources\romfs.bin" --type romfs

    sh build_daedalus.sh CTR_RELEASE

    copy "daedbuild\DaedalusX64.cia" "dist\%%~nI.cia"

    mkdir "dist\3ds\%%~nI" 2>nul

    copy "daedbuild\DaedalusX64.3dsx" "dist\3ds\%%~nI\%%~nI.3dsx"

    del "Source\SysCTR\Resources\romfs\%%~nxI"

    echo %%~nI done
)

cd "%curdir%"