#pragma once

#include <cstdint>

class CUser;

class CSteamEngine
{
public:
	CUser* getUser(uint32_t index);
	void setAppIdForCurrentPipe(uint32_t appId);
};

extern CSteamEngine* g_pSteamEngine;
