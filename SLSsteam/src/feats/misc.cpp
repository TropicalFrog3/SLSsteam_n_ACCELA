#include "misc.hpp"

#include "../config.hpp"
#include "../sdk/protobufs/steammessages_clientserver_appinfo.pb.h"
#include <fstream>
#include "../globals.hpp"

#include "fakeappid.hpp"


bool Misc::shouldFakeOffline()
{
	const AppId_t appId = FakeAppIds::getRealAppIdForCurrentPipe();
	if (!appId || !g_config.fakeOffline.get().contains(appId))
	{
		return false;
	}

	LOG_ONCE("Faking offline mode for %u\n", appId);
	return true;
}


void Misc::recvMsg(CNetPacket *pkt)
{
	switch(pkt->getProtoBufType())
	{
		case k_EMsgClientPersonaState:
		{
			const auto name = g_config.fakeName.get();
			if (name.size() < 1)
			{
				return;
			}

			auto msg = pkt->deserializeBody<CMsgClientPersonaState>();

			for (int i = 0; i < msg.friends_size(); i++)
			{
				auto frnd = msg.mutable_friends(i);

				if (frnd->friendid() != g_currentSteamId.steamId64)
				{
					continue;
				}

				frnd->set_player_name(name);
				LOG_DEBUG("Faked self persona\n");

				pkt->serialize(msg);
				break;
			}

			break;
		}

		case k_EMsgClientEmailAddrInfo:
		{
			const auto email = g_config.fakeEmail.get();
			if (email.size() < 1)
			{
				return;
			}

			auto msg = pkt->deserializeBody<CMsgClientEmailAddrInfo>();
			msg.set_email_address(email);
			msg.set_email_is_validated(true);

			pkt->serialize(msg);
			break;
		}

		case k_EMsgClientWalletInfoUpdate:
		{
			const int32_t amount = g_config.fakeWalletBalance.get();
			if (!amount)
			{
				return;
			}

			auto msg = pkt->deserializeBody<CMsgClientWalletInfoUpdate>();
			msg.set_has_wallet(true);
			msg.set_balance(amount);
			msg.set_balance64(amount);

			pkt->serialize(msg);
			break;
		}

		case k_EMsgClientPICSProductInfoResponse:
		{
			auto msg = pkt->deserializeBody<CMsgClientPICSProductInfoResponse>();
			LOG_INFO("PICS Response: meta_data_only=%d, http_host=%s\n", msg.meta_data_only(), msg.http_host().c_str());
			for (int i = 0; i < msg.apps_size(); i++)
			{
				auto* app = msg.mutable_apps(i);
				LOG_INFO("Got PICS info for %u, has_buffer: %d\n", app->appid(), app->has_buffer());
				if (app->has_buffer())
				{
					std::string path = "/tmp/pics_" + std::to_string(app->appid()) + ".bin";
					std::ofstream ofs(path, std::ios::binary);
					ofs.write(app->buffer().data(), app->buffer().size());
					ofs.close();
				}
			}
			break;
		}

		default:
			break;
	}
}

void Misc::sendMsg(CNetPacket *pkt)
{
	switch(pkt->getProtoBufType())
	{
		case k_EMsgClientPICSProductInfoRequest:
		{
			auto msg = pkt->deserializeBody<CMsgClientPICSProductInfoRequest>();
			LOG_INFO("PICS Request: meta_data_only=%d\n", msg.meta_data_only());
			
			for (int i = 0; i < msg.apps_size(); i++)
			{
				auto* app = msg.mutable_apps(i);
				LOG_INFO("Requesting PICS for app %u (has access token: %d)\n", app->appid(), app->has_access_token());
			}
			
			break;
		}
	}
}
