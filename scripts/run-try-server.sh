#!/bin/bash

cd ../source/tssx
make
cd -

if [ ${OSTYPE//[0-9.]/} = darwin ]; then
  DYLD_INSERT_LIBRARIES=$PWD/../source/tssx/libtssx-server.dylib \
  DYLD_FORCE_FLAT_NAMESPACE=1 ./try-server $1
else
  LD_PRELOAD=$PWD/../source/tssx/libtssx-server.so ./try-server
fi
