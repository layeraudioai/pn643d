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
    touch rom_locks\!foldername!
    del !foldername!\CMakeCache.txt
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
    mkdir !foldername!
    copy Source\SysCTR\Resources\template2.rsf !foldername!\!foldername!.rsf /Y
    xcopy Source\SysCTR\Resources\romfs\ !foldername!\romfs\ /E /Y 
    if exist Data\roms.ini copy Data\roms.ini !foldername!\romfs\roms.ini /Y
    del !foldername!\!foldername!.rsf
    for /f "delims=" %%L in (Source\SysCTR\Resources\template2.rsf) do (
      set "line=%%L"
      for %%a in (!tid!) do (
        set "line=!line:0xDAED3=%%a!"
      )
      for %%a in (!foldername!) do (
	      set "line=!line:DaedalusX64=%%a!"
      )
      echo !line!
    ) >> "!foldername!\!foldername!.rsf"
    copy %1% !foldername!\romfs\Roms\!foldername!.z64 /Y
    
    python Tools\generate_romdb_prefs.py %1% !foldername! !foldername!\romfs 
    python Tools\generate_banner_smdh.py %1% !foldername! !foldername!
    Tools\3dstool -c --type romfs --romfs-dir !foldername!\romfs --file !foldername!\!foldername!.bin
    sh build_daedalus.sh CTR_RELEASE !foldername!
    move !foldername!\!foldername!.cia dist\!foldername!.cia 
    mkdir dist\3ds\!foldername!
    move !foldername!\!foldername!.3dsx dist\3ds\!foldername!\!foldername!.3dsx
    move %1% ..\used\
    move !foldername! cmakedirs
    echo !foldername! done && del rom_locks\!foldername!