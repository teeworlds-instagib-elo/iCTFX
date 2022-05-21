#!/bin/bash

mkdir -p bin
pushd bin
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DMYSQL=1 -DCLIENT=0 ..
cp compile_commands.json ..
popd
