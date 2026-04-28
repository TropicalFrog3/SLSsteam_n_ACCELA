#!/bin/bash

CONFIG="$(cat "./res/config.yaml")"

echo "static const char* defaultConfig = R\"($CONFIG)\";" > src/config_default.hpp
