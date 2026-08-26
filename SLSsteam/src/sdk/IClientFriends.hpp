#pragma once

#include "types.hpp"

#include <cstdint>


SDK_Struct GamePlayed_t
{
	AppId_t appId;			//0x0
	uint8_t pad[0x14];		//0x4
}; //0x18
