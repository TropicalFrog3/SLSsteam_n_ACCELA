#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd "$SCRIPT_DIR"

g++ main.cpp -O3 -m32 -fPIC -shared -std=c++20 -o library-inject.so
