#!/bin/bash
rm -rf build && cmake -B build -S src/ -DCMAKE_BUILD_TYPE=Debug && cmake --build build -- -j$(nproc) 
cd build