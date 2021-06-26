#!/bin/sh
set -ex

cp LICENSE data/LICENSE-$PLUGIN_NAME

sed -i 's;${CMAKE_INSTALL_FULL_LIBDIR};/usr/lib;' CMakeLists.txt

mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j4
