#!/bin/zsh

cd ../source/tssx
make
cd -

DYLD_INSERT_LIBRARIES=$PWD/../source/tssx/libtssx-server.dylib \
DYLD_FORCE_FLAT_NAMESPACE=1 ./try-server $1
