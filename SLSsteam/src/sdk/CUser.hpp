#pragma once

#include "EResult.hpp"

#include <cstdint>

class CAppOwnershipInfo;

enum class ECallbackType : uint32_t
{
	LicensesUpdate_t = 0x7d,
	AppOwnershipTicketReceived_t = 0xf907c,
};

struct AppOwnershipTicketReceived_t
{
	EResult result;
	uint32_t appId;
};

class CUser
{
public:
	bool checkAppOwnership(uint32_t appId, CAppOwnershipInfo* pInfo);
	bool isSubscribed(uint32_t appId);

	void postCallback(ECallbackType type, void* pCallback, uint32_t callbackSize);
	void updateAppOwnershipTicket(uint32_t appId, void* pTicket, uint32_t len);
};
