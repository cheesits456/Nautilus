#!/bin/bash
echo "Getting root privledges . . ."
sudo echo "Done!"
echo
rm -rf build
mkdir build
cd build
meson --prefix=/usr      \
      --sysconfdir=/etc  \
      -Dselinux=false    \
      -Dpackagekit=false \
      ..
ninja && sudo ninja install
cd ..
