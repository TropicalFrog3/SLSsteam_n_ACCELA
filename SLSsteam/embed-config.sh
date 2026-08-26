#!/bin/bash

HEADER_FILE="src/config_default.hpp"

DEFAULT_CONFIG="$(cat "./res/config.yaml")"
EMBEDED_CONFIG="$(cat "$HEADER_FILE")"

NEW_EMBEDED="#pragma once

constexpr static const char* defaultConfig = R\"($DEFAULT_CONFIG)\";"

#Do not update the config when nothing changed
#Otherwise the makefile will keep recompiling it when switching
#compiler flags/compilers
if [ "$EMBEDED_CONFIG" != "$NEW_EMBEDED" ]; then
	echo "$NEW_EMBEDED" > "$HEADER_FILE"
fi
