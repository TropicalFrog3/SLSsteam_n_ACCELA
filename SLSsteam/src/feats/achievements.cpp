#include "achievements.hpp"

#include "../curl.hpp"
#include "../config.hpp"
#include "../log.hpp"

#include <cstdint>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>


std::unordered_map<AppId_t, uint64_t> Achievements::preferredOwners;
std::unordered_map<AppId_t, std::unordered_set<uint64_t>> Achievements::ownerBlacklist;

std::string Achievements::getReviewUrl(const AppId_t appId)
{
	std::ostringstream url;
	url << "https://store.steampowered.com/appreviews/" << appId
		<< "?json=1&filter=recent&language=all&purchase_type=all&num_per_page="
		<< g_config.maxSchemaTries.get();

	return url.str();
}

std::unordered_set<uint64_t> Achievements::getReviewersForGame(const AppId_t appId)
{
	auto list = std::unordered_set<uint64_t>();
	if (!g_config.maxSchemaTries.get())
	{
		return list;
	}

	auto url = getReviewUrl(appId);
	std::string reviews;

	if (Curl::getString(url.c_str(), reviews))
	{
		LOG_WARN("Failed to get reviewer list for %u!\n", appId);
		return list;
	}

	if (!ownerBlacklist.contains(appId))
	{
		ownerBlacklist[appId] = std::unordered_set<uint64_t>();
	}

	//LOG_DEBUG("Downloaded reviewers %s\n", reviews.c_str());

	std::regex steamIdFieldsRe("\"steamid\":\"[0-9]+\"");

	auto begin = std::sregex_iterator(reviews.begin(), reviews.end(), steamIdFieldsRe);
	auto end = std::sregex_iterator();

	for (auto i = begin; i != end; ++i)
	{
		std::smatch steamIdMatch = *i;
		std::string steamIdFieldStr = steamIdMatch.str();

		//LOG_DEBUG("SteamId match %s\n", match.str().c_str());

		std::regex idRe("[0-9]+");
		if (!std::regex_search(steamIdFieldStr, steamIdMatch, idRe))
		{
			continue;
		}

		LOG_DEBUG("Extracted SteamId %s\n", steamIdMatch.str().c_str());

		uint64_t steamId = std::stoull(steamIdMatch.str().c_str());
		if (ownerBlacklist[appId].contains(steamId))
		{
			LOG_DEBUG("Skipping %llu for %u because it has failed before\n", steamId, appId);
			continue;
		}

		list.emplace(steamId);
	}

	return list;
}


uint32_t Achievements::tryGetPlayerStats
(
	CClientUnifiedServiceTransport* serviceTransport,
	const char* serviceName,
	CPlayer_GetUserStats_Request* send,
	CPlayer_GetUserStats_Response* recv,
	const uint64_t steamId
)
{
	LOG_DEBUG("CPlayer_GetUserStats_Request->set_steamid(%llu)\n", steamId);
	send->set_steamid(steamId);
	uint32_t res = serviceTransport->sendAndRecvMsg(serviceName, send, recv);

	const AppId_t appId = send->appid();

	if (res == k_EResultFailure)
	{
		ownerBlacklist[appId].emplace(steamId);
		return k_EResultNoResult;
	}
	else if (res != k_EResultOK)
	{
		return k_EResultNoResult;
	}

	recv->clear_crc_stats();
	recv->clear_stats();

	preferredOwners[appId] = steamId;
	LOG_DEBUG("Using steamId %llu for %u\n", steamId, appId);
	return k_EResultOK;
}

uint32_t Achievements::sendAndRecvGetPlayerStats
(
	CClientUnifiedServiceTransport* serviceTransport,
	const char* serviceName,
	CPlayer_GetUserStats_Request* send,
	CPlayer_GetUserStats_Response* recv
)
{
	if (strcmp(serviceName, GET_PLAYER_STATS_SERVICE_NAME) != 0)
	{
		return k_EResultNoResult;
	}

	//Don't do anything for legit apps
	if (g_pSteamEngine->getUser(0)->isSubscribed(send->appid()))
	{
		return k_EResultNoResult;
	}

	const AppId_t appId = send->appid();
	send->clear_crc_stats();

	//Prefer last successfull owner to skip review fetch. Fixes stutters caused
	//by review fetching in games that spam stat requests
	if (preferredOwners.contains(appId))
	{
		const uint32_t res = tryGetPlayerStats(serviceTransport, GET_PLAYER_STATS_SERVICE_NAME, send, recv, preferredOwners.at(appId));
		if (res == k_EResultOK)
		{
			return res;
		}

		preferredOwners.erase(send->appid());
	}

	const auto reviewers = getReviewersForGame(send->appid());

	for (const auto& id : reviewers)
	{
		const uint32_t res = tryGetPlayerStats(serviceTransport, serviceName, send, recv, id);
		if (res == k_EResultOK)
		{
			return res;
		}
	}

	LOG_DEBUG("No schemas for %u found! Falling back to offline cache\n", appId);
	return k_EResultNoConnection;
}

uint32_t Achievements::tryGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, const uint32_t timeOut, CProtoBufMsgBase* recv, const EMsg targetType, const uint64_t steamId)
{
	const auto sendBdy = send->getBody<CMsgClientGetUserStats>();

	LOG_DEBUG("CMsgClientGetUserStats->set_steam_id_for_user(%llu)\n", steamId);
	sendBdy->set_steam_id_for_user(steamId);

	const uint32_t ret = job->sendAndRecv(send, timeOut, recv, targetType);
	if (!ret)
	{
		return 0;
	}

	const AppId_t appId = sendBdy->game_id();
	const auto recvBdy = recv->getBody<CMsgClientGetUserStatsResponse>();

	if (recvBdy->eresult() == k_EResultFailure)
	{
		ownerBlacklist[appId].emplace(steamId);
		return 0;
	}

	if (recvBdy->eresult() != k_EResultOK)
	{
		return 0;
	}

	recvBdy->clear_achievement_blocks();
	recvBdy->clear_crc_stats();
	recvBdy->clear_stats();

	LOG_DEBUG("Using steamId %llu for %u\n", steamId, appId);
	preferredOwners[appId] = steamId;
	return ret;
}

uint32_t Achievements::sendAndRecvGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, const uint32_t timeOut, CProtoBufMsgBase* recv, const EMsg targetType)
{
	if (targetType != k_EMsgClientGetUserStatsResponse)
	{
		return 0;
	}

	const auto sendBdy = send->getBody<CMsgClientGetUserStats>();

	if (g_pSteamEngine->getUser(0)->isSubscribed(sendBdy->game_id()))
	{
		return 0;
	}

	const AppId_t appId = sendBdy->game_id();
	sendBdy->clear_crc_stats();

	if (preferredOwners.contains(appId))
	{
		uint32_t ret = tryGetUserStats(job, send, timeOut, recv, targetType, preferredOwners.at(appId));
		if (ret)
		{
			return ret;
		}

		//Prefered owner failed, erase then fallthrough to trying reviewers
		preferredOwners.erase(appId);
	}

	const auto reviewers = getReviewersForGame(sendBdy->game_id());

	for (const auto& id : reviewers)
	{
		const uint32_t ret = tryGetUserStats(job, send, timeOut, recv, targetType, id);
		if (ret)
		{
			return ret;
		}
	}

	const auto recvBdy = recv->getBody<CMsgClientGetUserStatsResponse>();
	recvBdy->set_eresult(k_EResultNoConnection);
	LOG_DEBUG("No schemas for %u found! Falling back to offline cache\n", appId);
	return 1;
}
