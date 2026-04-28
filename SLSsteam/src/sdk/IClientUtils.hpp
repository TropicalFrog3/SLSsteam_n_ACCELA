#pragma once

#include <cstdint>

class IClientUtils
{
public:
	uint32_t* getPipeIndex();
	uint32_t getAppId();
};

extern IClientUtils* g_pClientUtils;
