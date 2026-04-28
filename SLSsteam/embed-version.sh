#!/bin/bash

VERSION="$(cat "./res/version.txt")"

echo "#pragma once

#include <cstdint>


constexpr uint64_t VERSION = $VERSION;" > src/version.hpp
