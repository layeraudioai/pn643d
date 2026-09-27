@ECHO OFF
setlocal enabledelayedexpansion
    set "foldername=%2%"
    set "foldername=!foldername: =!"
    set "foldername=!foldername:(=!"
    set "foldername=!foldername:)=!"
    set "foldername=!foldername:-=!"
    set "foldername=!foldername:,=!"
    set "foldername=!foldername:\=!"
    set "foldername=!foldername:/=!"
    set "foldername=!foldername:-=!"
    set "foldername=!foldername:"=!"
    set "foldername=!foldername:'=!"
    set "foldername=!foldername:\!=!"
    set "foldername=!foldername:[=!"
    set "foldername=!foldername:]=!"
    set "foldername=!foldername:{=!"
    set "foldername=!foldername:}=!"
    set "foldername=!foldername:.=!"
    set "foldername=!foldername:&=!"
    touch "rom_locks\!foldername!"
    
    del "Source\SysCTR\Resources\!foldername!\romfs\Roms\*.*64" 
    del "!foldername!\CMakeCache.txt" 
    copy Source\SysCTR\Resources\template2.rsf "!foldername!\!foldername!.rsf" 
    xcopy Source\SysCTR\Resources\romfs\ "!foldername!\romfs\" /E /Y 
    if exist Data\roms.ini copy Data\roms.ini "!foldername!\romfs\roms.ini" /Y
    python Tools\generate_romdb_prefs.py %1% "!foldername!" "!foldername!\romfs"
    tools\3dstool -c --type romfs --romfs-dir "!foldername!\romfs" --file "!foldername!\!foldername!.bin" 
    set "hex=!foldername!"
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
    echo !foldername! TID=!tid!
    Tools\additionals\u64aap -a -g -d -f -l -n -k -i %1% -o "!foldername!\romfs\Roms\!foldername!.z64"
    (
    for /f "delims=" %%L in (Source\SysCTR\Resources\template2.rsf) do (
      set "line=%%L"
      for %%a in (!tid!) do (
        set "line=!line:0xDAED3=%%a!"
      )
      for %%a in (!foldername!) do (
	set "line=!line:DaedalusX64=%%a!"
      )
      echo !line!
    )
    ) > !foldername!\!foldername!.rsf
    python Tools\generate_banner_smdh.py %1% "!foldername!" "!foldername!"
    tools\3dstool -c --type romfs --romfs-dir "!foldername!\romfs" --file "!foldername!\!foldername!.bin" 
    sh build_daedalus.sh CTR_RELEASE "!foldername!" 
    move "!foldername!\!foldername!.cia" "dist\!foldername!.cia" 
    mkdir "dist\3ds\!foldername!" 
    move "!foldername!\!foldername!.3dsx" "dist\3ds\!foldername!\!foldername!.3dsx" 
    del "!foldername!\Roms\!foldername!.*64" 
    echo !foldername! done
    del "rom_locks\!foldername!"
    move "..\!rom_file!" ..\used
    del "rom_locks\!foldername!"
exit