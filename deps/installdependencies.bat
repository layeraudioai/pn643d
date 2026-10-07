devkitProUpdater-3.0.3
pacman -Slq dkp-libs | pacman -Sy - --needed --noconfirm
cd picaGL
make install
cd ..\imgui-picagl
make install
cd ..
cp /usr/bin/ffmpeg.exe /usr/bin/ffmpeg-bak.exe
..\Tools\unzip ffmpeg.zip
cp ffmpeg.exe /usr/bin