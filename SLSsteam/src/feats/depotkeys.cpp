#include "depotkeys.hpp"

#include "../config.hpp"
#include "../globals.hpp"
#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/EResult.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>

// EMSG values (from SteamKit EMsg)
// Note: EMSG_APPOWNERSHIPTICKET_RESPONSE (858) is already in EMsgType enum
static constexpr uint16_t EMSG_DEPOT_DECRYPTION_KEY_REQUEST  = 5438;
static constexpr uint16_t EMSG_DEPOT_DECRYPTION_KEY_RESPONSE = 5439;

static DepotKeys::KeyMap g_depotKeys;
static std::mutex g_depotKeysMutex;

static std::string hexToBytes(const std::string& hex)
{
	std::string bytes;
	bytes.reserve(hex.size() / 2);
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
	{
		unsigned int byte;
		std::sscanf(hex.c_str() + i, "%02x", &byte);
		bytes.push_back(static_cast<char>(byte));
	}
	return bytes;
}

void DepotKeys::scanLuaPluginsForDepotKeys()
{
	std::string pluginDir = g_config.getPluginDir();
	if (pluginDir.empty())
	{
		g_pLog->warn("DepotKeys: Cannot locate plugin directory\n");
		return;
	}

	if (!std::filesystem::exists(pluginDir))
	{
		return;
	}

	// Match: addappid(depot_id, 0, "hex_key")
	// The depot key lines look like: addappid(3618391,0,"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9")
	const std::regex depotKeyRegex(R"re(addappid\(\s*(\d+)\s*,\s*\d+\s*,\s*"([0-9a-fA-F]+)"\s*\))re");

	KeyMap newKeys;

	for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
	{
		if (!entry.is_regular_file())
			continue;

		if (entry.path().extension() != ".lua")
			continue;

		std::ifstream file(entry.path());
		if (!file.is_open())
			continue;

		std::string content((std::istreambuf_iterator<char>(file)),
							 std::istreambuf_iterator<char>());
		file.close();

		// Find all depot key entries in this lua file
		auto begin = std::sregex_iterator(content.begin(), content.end(), depotKeyRegex);
		auto end = std::sregex_iterator();

		for (auto it = begin; it != end; ++it)
		{
			const std::smatch& match = *it;
			try
			{
				const uint32_t depotId = static_cast<uint32_t>(std::stoul(match[1].str()));
				const std::string hexKey = match[2].str();

				if (hexKey.size() == 64) // 32 bytes = 64 hex chars (AES-256)
				{
					newKeys[depotId] = hexKey;
					g_pLog->info("DepotKeys: Loaded key for depot %u (from %s)\n",
								 depotId, entry.path().filename().c_str());
				}
				else
				{
					g_pLog->warn("DepotKeys: Unexpected key length %zu for depot %u\n",
								 hexKey.size(), depotId);
				}
			}
			catch (...)
			{
				g_pLog->warn("DepotKeys: Failed to parse depot key in %s\n",
							 entry.path().c_str());
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_depotKeysMutex);
		g_depotKeys = std::move(newKeys);
	}

	g_pLog->info("DepotKeys: Scan complete, loaded %zu depot key(s)\n", g_depotKeys.size());
}

std::string DepotKeys::getDepotKey(uint32_t depotId)
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	auto it = g_depotKeys.find(depotId);
	if (it != g_depotKeys.end())
	{
		return it->second;
	}
	return "";
}

bool DepotKeys::isAccelaApp(uint32_t appId)
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	// An app is ACCELA-managed if we have any depot key for it or its depots
	// Depot IDs are typically appId or appId+1, etc.
	// But the most reliable check: does the Lua plugin exist?
	std::string pluginDir = g_config.getPluginDir();
	if (pluginDir.empty()) return false;
	std::string luaPath = pluginDir + "/" + std::to_string(appId) + ".lua";
	return std::filesystem::exists(luaPath);
}

static void handleDepotDecryptionKeyResponse(CProtoBufMsgBase* msg)
{
	auto body = msg->getBody<CMsgClientGetDepotDecryptionKeyResponse>();

	const uint32_t depotId = body->depot_id();
	const int32_t result = body->eresult();

	g_pLog->info("DepotKeys: Received decryption key response for depot %u, eresult=%d\n",
				 depotId, result);

	if (result == ERESULT_OK)
	{
		g_pLog->debug("DepotKeys: Depot %u key received from Steam (OK)\n", depotId);
		return;
	}

	const std::string hexKey = DepotKeys::getDepotKey(depotId);
	if (hexKey.empty())
	{
		g_pLog->info("DepotKeys: No cached key for depot %u (eresult=%d), cannot inject\n",
					 depotId, result);
		return;
	}

	const std::string keyBytes = hexToBytes(hexKey);

	CMsgClientGetDepotDecryptionKeyResponse localBody;
	if (localBody.ParseFromString(body->SerializeAsString()))
	{
		localBody.set_eresult(ERESULT_OK);
		localBody.set_depot_encryption_key(keyBytes.data(), keyBytes.size());
		body->ParseFromString(localBody.SerializeAsString());

		g_pLog->info("DepotKeys: Injected cached key for depot %u (overriding eresult %d -> OK)\n",
					 depotId, result);
	}
	else
	{
		g_pLog->warn("DepotKeys: Failed to parse depot key response for depot %u\n", depotId);
	}
}

static void handleOwnershipTicketResponse(CProtoBufMsgBase* msg)
{
	auto body = msg->getBody<CMsgClientGetAppOwnershipTicketResponse>();

	const uint32_t appId = body->app_id();
	const uint32_t result = body->eresult();

	// If successful, let the existing ticket system handle caching
	if (result == ERESULT_OK)
	{
		return;
	}

	// Only intercept for ACCELA-managed apps
	if (!DepotKeys::isAccelaApp(appId))
	{
		return;
	}

	g_pLog->info("DepotKeys: Ownership ticket failed for ACCELA app %u (eresult=%u), injecting OK\n",
				 appId, result);

	// Set eresult to OK with an empty ticket — this tells Steam the app is "owned"
	// The actual ownership validation is already handled by SLSsteam's app hooks
	CMsgClientGetAppOwnershipTicketResponse localBody;
	if (localBody.ParseFromString(body->SerializeAsString()))
	{
		localBody.set_eresult(ERESULT_OK);
		// Set a 50-byte dummy ticket that is structurally valid so Steam doesn't fail parsing
		std::string dummyTicket(50, '\0');
		uint32_t* pU32 = reinterpret_cast<uint32_t*>(dummyTicket.data());
		pU32[0] = 50; // length
		pU32[1] = 4;  // version
		uint64_t steamId = 0x0110000100000000ULL | g_currentSteamId;
		*reinterpret_cast<uint64_t*>(dummyTicket.data() + 8) = steamId;
		pU32[4] = appId; // offset 16 is appId

		localBody.set_ticket(dummyTicket);
		body->ParseFromString(localBody.SerializeAsString());

		g_pLog->info("DepotKeys: Injected ownership ticket OK (with dummy struct) for app %u\n", appId);
	}
	else
	{
		g_pLog->warn("DepotKeys: Failed to parse ownership ticket response for app %u\n", appId);
	}
}

void DepotKeys::recvMsg(CProtoBufMsgBase* msg)
{
	switch (msg->type)
	{
		case EMSG_DEPOT_DECRYPTION_KEY_RESPONSE:
			handleDepotDecryptionKeyResponse(msg);
			break;

		case EMSG_APPOWNERSHIPTICKET_RESPONSE:
			handleOwnershipTicketResponse(msg);
			break;
	}
}

void DepotKeys::sendMsg(CProtoBufMsgBase* msg)
{
	if (msg->type != EMSG_DEPOT_DECRYPTION_KEY_REQUEST)
	{
		return;
	}

	auto body = msg->getBody<CMsgClientGetDepotDecryptionKey>();

	g_pLog->info("DepotKeys: Requesting decryption key for depot %u (app %u)\n",
				 body->depot_id(), body->app_id());
}
