#!/bin/bash

if [ ${OSTYPE//[0-9.]/} = darwin ]; then
  DYLD_INSERT_LIBRARIES=$PWD/../source/tssx/libtssx-client.dylib \
  DYLD_FORCE_FLAT_NAMESPACE=1 ./try-client
else
  LD_PRELOAD=$PWD/../source/tssx/libtssx-client.so ./try-client
fi
