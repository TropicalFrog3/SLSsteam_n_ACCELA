#include "IClientAppManager.hpp"

#include "../memhlp.hpp"
#include "../vftableinfo.hpp"

#include <cstdint>

bool IClientAppManager::installApp(uint32_t appId, uint32_t librarIndex)
{
	return MemHlp::callVFunc<bool(*)(void*, uint32_t, uint32_t, uint8_t)>(VFTIndexes::IClientAppManager::InstallApp, this, appId, librarIndex, 0);
}

EAppState IClientAppManager::getAppInstallState(uint32_t appId)
{
	return MemHlp::callVFunc<EAppState(*)(void*, uint32_t)>(VFTIndexes::IClientAppManager::GetAppInstallState, this, appId);
}

IClientAppManager* g_pClientAppManager;
