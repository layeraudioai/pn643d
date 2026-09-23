set curdir=%CD%
cd picaGL && make && make install && cd ../imgui-picagl && make && make install && cd ../DaedalusX64-3DS && sh build_daedalus.sh CTR_RELEASE && cd ..
cd %curdir%
