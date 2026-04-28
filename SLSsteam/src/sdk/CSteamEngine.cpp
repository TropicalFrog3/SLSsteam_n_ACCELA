#include "CSteamEngine.hpp"

#include "../hooks.hpp"
#include "../patterns.hpp"

#include "libmem/libmem.h"


CUser* CSteamEngine::getUser(uint32_t index)
{
	const static auto offset = *reinterpret_cast<lm_address_t*>(Patterns::CSteamEngine::Offset_User.address + 0x2);
	const auto ppUserMap = *reinterpret_cast<uint8_t**>(this + offset);
	const auto ppUser = ppUserMap + index * 8;

	return *reinterpret_cast<CUser**>(ppUser + 4);
}

void CSteamEngine::setAppIdForCurrentPipe(uint32_t appId)
{
	//Last argument needs to be 0, otherwise steam crashes.
	//Might be only 1 when steam first sets it, then 0
	Hooks::CSteamEngine_SetAppIdForCurrentPipe.tramp.fn(this, appId, 0);
}

CSteamEngine* g_pSteamEngine = nullptr;
