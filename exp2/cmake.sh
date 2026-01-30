#!/bin/bash
if [ -d "build" ]; then
    echo "detected to have build dir."
    rm -rf build
fi
cmake -B build -S .  -DCMAKE_C_COMPILER=/usr/bin/gcc-9 -DCMAKE_CXX_COMPILER=/usr/bin/g++-9

