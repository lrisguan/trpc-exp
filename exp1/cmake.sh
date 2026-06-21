#!/bin/bash
if [ -d "build" ]; then
    echo "detected to have build dir."
    rm -rf build
fi
cmake -S . -B build
make -C build -j
