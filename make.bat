set curdir=%CD%
cd picaGL && make && make install && cd ../imgui-picagl && make && make install && cd ../DaedalusX64-3DS
FOR %%I in (..\roms\*.*64) DO (
  copy %%I Source\SysCTR\Resources\romfs
  tools\ctrtool --romfsdir Source\SysCTR\Resources\romfs Source\SysCTR\Resources\romfs.bin
  sh build_daedalus.sh CTR_RELEASE
  copy daedbuild\DaedalusX64.cia dist\%%~nI.cia
  mkdir dist\3ds\%%~nI
  copy daedbuild\DaedalusX64.3dsx dist\3ds\%%~nI\%%~nI.3dsx
  del Source\SysCTR\Resources\romfs\%%~nI
)
cd %curdir%
