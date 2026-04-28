#pragma once

#include "libmem/libmem.h"

#include <cstdint>

class CSteamId
{
public:
	uint32_t steamId; //0x0
	uint16_t type; //0x4 - Might be universe aswell
	uint8_t instance; //0x6
	uint8_t universe; //0x7 Might be type aswell
}; //0x8
