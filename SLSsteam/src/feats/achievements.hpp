#pragma once

#include "../sdk/sdk.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>


namespace Achievements
{
	constexpr const char* GET_PLAYER_STATS_SERVICE_NAME = "Player.GetUserStats#1";

	extern std::unordered_map<AppId_t, uint64_t> preferredOwners;
	extern std::unordered_map<AppId_t, std::unordered_set<uint64_t>> ownerBlacklist;

	std::string getReviewUrl(const AppId_t appId);
	std::unordered_set<uint64_t> getReviewersForGame(const AppId_t appId);

	uint32_t tryGetPlayerStats
	(
		CClientUnifiedServiceTransport* serviceTransport,
		const char* serviceName,
		CPlayer_GetUserStats_Request* send,
		CPlayer_GetUserStats_Response* recv,
		const uint64_t steamId
	);
	//CPlayer_GetUserStats
	uint32_t sendAndRecvGetPlayerStats
	(
		CClientUnifiedServiceTransport* serviceTransport,
		const char* serviceName,
		CPlayer_GetUserStats_Request* send,
		CPlayer_GetUserStats_Response* recv
	);

	uint32_t tryGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, const uint32_t timeOut, CProtoBufMsgBase* recv, const EMsg targetType, const uint64_t steamId);
	//GetUserStats
	uint32_t sendAndRecvGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, const uint32_t timeOut, CProtoBufMsgBase* recv, const EMsg targetType);
}
