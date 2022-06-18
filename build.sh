#!/bin/bash

ld
pushd bin
make -j$(nproc)
popd
