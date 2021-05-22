#!/bin/sh
set -ex

mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -D ENABLE_PROFILE=ON ..
make -j4
