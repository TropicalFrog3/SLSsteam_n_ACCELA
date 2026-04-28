#pragma once

#include <cstddef>
#include <cstdint>


class CAppOwnershipInfo;

namespace DLC
{
	bool shouldUnlockDlc(uint32_t appId);

	bool checkAppOwnership(uint32_t appId, CAppOwnershipInfo* info);
	bool isDlcEnabled(uint32_t appId);
	bool isAppDlcInstalled(uint32_t appId);
	bool userSubscribedInTicket(uint32_t appId);

	uint32_t getDlcCount(uint32_t appId);
	bool getDlcDataByIndex(uint32_t appId, int index, uint32_t* dlcId, bool* available, char* dlcName, size_t& dlcNameLen);
}
