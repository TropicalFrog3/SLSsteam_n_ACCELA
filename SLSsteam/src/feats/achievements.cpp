#include "achievements.hpp"

#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/EResult.hpp"

#include "../log.hpp"


void Achievements::getPlayerStats(uint32_t& eresult)
{
	if (eresult == ERESULT_OK)
	{
		return;
	}

	eresult = ERESULT_NO_CONNECTION;
}

void Achievements::recvMessage(const CProtoBufMsgBase* msg)
{
	if (msg->type != EMSG_REQUEST_USERSTATS_RESPONSE)
	{
		return;
	}

	const auto body = msg->getBody<CMsgClientGetUserStatsResponse>();

	if (body->eresult() == ERESULT_OK)
	{
		return;
	}

	body->set_eresult(ERESULT_NO_CONNECTION);
	g_pLog->debug("Forcing offline stat usage for %u\n", body->game_id());
}
