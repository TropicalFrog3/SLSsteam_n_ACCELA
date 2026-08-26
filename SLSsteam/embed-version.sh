#!/bin/bash

HEADER_FILE="src/version.hpp"

VERSION="$(grep '^Version:' ./res/version | cut -d' ' -f2 | xargs)"
EMBEDED_VERSION="$(cat "$HEADER_FILE")"

NEW_EMBEDED="#pragma once

#include <cstdint>


constexpr const char* VERSION = \"$VERSION\";"

#Do not update version when nothing changed
#Otherwise the makefile will keep recompiling it when switching
#compiler flags/compilers
if [ "$EMBEDED_VERSION" != "$NEW_EMBEDED" ]; then
	echo "$NEW_EMBEDED" > "$HEADER_FILE"
fi
