#!/bin/sh
set -ex

cp LICENSE data/LICENSE-$PLUGIN_NAME
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j4
