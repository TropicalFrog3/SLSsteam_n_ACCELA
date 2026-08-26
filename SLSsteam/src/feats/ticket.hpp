#pragma once

#include "../sdk/sdk.hpp"

#include <string>
#include <unordered_map>


namespace Ticket
{
	class SavedTicket
	{
public:
		CSteamId steamId;
		std::string ticket;

		constexpr bool isValid() const
		{
			return steamId.isSet() && ticket.size() > 0;
		}
	};

	extern std::unordered_map<AppId_t, CSteamId> oneTimeSteamIdSpoof;
	extern std::unordered_map<AppId_t, SavedTicket> ticketMap;
	extern std::unordered_map<AppId_t, SavedTicket> encryptedTicketMap;

	std::string getTicketDir();

	//TODO: Fill with error checks
	std::string getTicketPath(const AppId_t appId);
	SavedTicket* getCachedTicket(const AppId_t appId);
	bool saveTicketToCache(const CMsgClientGetAppOwnershipTicketResponse& resp);

	void launchApp(const AppId_t appId);
	void getEncryptedAppTicket(const AppId_t appId);
	void getTicketOwnershipExtendedData(const AppId_t appId);

	std::string getEncryptedTicketPath(const AppId_t appId);
	SavedTicket* getCachedEncryptedTicket(const AppId_t appId);
	bool saveEncryptedTicketToCache(const CMsgClientRequestEncryptedAppTicketResponse& resp);

	void recvEncryptedAppTicket(CNetPacket* pkt);
	void recvAppTicket(const CNetPacket* pkt);
	void recvMsg(CNetPacket* pkt);
}
