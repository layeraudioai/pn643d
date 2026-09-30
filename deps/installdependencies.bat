devkitProUpdater-3.0.3
pacman -Slq dkp-libs | pacman -Sy - --needed --noconfirm
cd picaGL
make install
cd ..\imgui-picagl
make install