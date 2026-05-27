#!/bin/bash

VERSION="$(grep '^Version:' ./res/version | cut -d' ' -f2 | xargs)"

echo "#pragma once


constexpr const char* VERSION = \"$VERSION\";" > src/version.hpp
