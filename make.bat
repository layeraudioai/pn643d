@ECHO OFF
set curdir="%CD%"
echo compile and install dependencies
cd picaGL && make && make install && cd ../imgui-picagl && make && make install && cd ../DaedalusX64-3DS
echo next step
pause
echo here wii go
FOR %%I in (..\roms\*.*64) DO (
  del daedbuild\CMakeCache.txt
  echo %%~nI
  copy "%%I" "Source\SysCTR\Resources\romfs"
  pause
  echo > "Source\SysCTR\Resources\romfs.bin"
  tools\3dstool -c --romfs-dir "Source\SysCTR\Resources\romfs" --file "Source\SysCTR\Resources\romfs.bin" --type romfs
  sh build_daedalus.sh CTR_RELEASE
  copy "daedbuild\DaedalusX64.cia" "dist\%%~nI.cia"
  mkdir "dist\3ds\%%~nI"
  copy "daedbuild\DaedalusX64.3dsx" "dist\3ds\%%~nI\%%~nI.3dsx"
  del "Source\SysCTR\Resources\romfs\%%~nI"
  echo %%~nI done
)
cd "%curdir%"
