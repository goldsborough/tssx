#!/bin/zsh

DYLD_INSERT_LIBRARIES=$PWD/../source/tssx/libtssx-client.dylib \
DYLD_FORCE_FLAT_NAMESPACE=1 ./try-client
