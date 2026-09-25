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
    
    del "Source\SysCTR\Resources\!foldername!\Roms\*.*64" 
    del "!foldername!\CMakeCache.txt" 
    copy Source\SysCTR\Resources\template2.rsf "Source\SysCTR\Resources\!foldername!.rsf" 
    xcopy Source\SysCTR\Resources\romfs\ "Source\SysCTR\Resources\!foldername!\" /E /Y 
    tools\3dstool -c --type romfs --romfs-dir "Source\SysCTR\Resources\!foldername!" --file "Source\SysCTR\Resources\!foldername!.bin" 
    sh build_daedalus.sh CTR_RELEASE "!foldername!" 
    copy "!foldername!\!foldername!.3dsx" "Source\SysCTR\Resources\!foldername!" 
    del "!foldername!\!foldername!.*" 
    del "Source\SysCTR\Resources\!foldername!\!foldername!.3dsx" 
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
    copy %1% "Source\SysCTR\Resources\!foldername!\Roms"
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
    ) > Source\SysCTR\Resources\!foldername!.rsf
    tools\3dstool -c --type romfs --romfs-dir "Source\SysCTR\Resources\!foldername!" --file "Source\SysCTR\Resources\!foldername!.bin" 
    sh build_daedalus.sh CTR_RELEASE "!foldername!" 
    copy "!foldername!\!foldername!.cia" "dist\!foldername!.cia" 
    mkdir "dist\3ds\!foldername!" 
    copy "!foldername!\!foldername!.3dsx" "dist\3ds\!foldername!\!foldername!.3dsx" 
    del "Source\SysCTR\Resources\!foldername!\Roms\!foldername!.*64" 
    echo !foldername! done
    del "rom_locks\!foldername!"