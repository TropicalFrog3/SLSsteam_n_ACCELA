#include "apps.hpp"

#include "../sdk/CAppOwnershipInfo.hpp"
#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/CSteamEngine.hpp"
#include "../sdk/CUser.hpp"
#include "../sdk/EReleaseState.hpp"
#include "../sdk/IClientApps.hpp"

#include "../config.hpp"
#include "../globals.hpp"

#include "fakeappid.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

bool Apps::applistRequested;
std::map<uint32_t, int> Apps::appIdOwnerOverride;
std::set<uint32_t> Apps::installedApps;

static std::filesystem::path getInstalledAppsPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::filesystem::path(home) / ".SLSsteam.installed";
}

static void reloadInstalledApps()
{
    auto path = getInstalledAppsPath();
    if (path.empty() || !std::filesystem::exists(path)) return;

    std::ifstream file(path);
    uint32_t id;
    while (file >> id)
    {
        Apps::installedApps.insert(id);
    }
}
void Apps::init()
{
    reloadInstalledApps();
}

bool Apps::isInstalled(uint32_t appId)
{
    return installedApps.contains(appId);
}

void Apps::setInstalled(uint32_t appId)
{
    g_pLog->info("Apps::setInstalled(%u)\n", appId);
    installedApps.insert(appId);

    auto path = getInstalledAppsPath();
    if (path.empty()) return;

    std::ofstream file(path, std::ios::app);
    file << appId << std::endl;
}

void Apps::removeInstalled(uint32_t appId)
{
    g_pLog->info("Apps::removeInstalled(%u)\n", appId);
    installedApps.erase(appId);

    auto path = getInstalledAppsPath();
	g_pLog->info("removeInstalled: Path: %s\n", path.string().c_str());
    if (path.empty()) return;

    // Rewrite the persistence file without the removed appId
    std::ofstream file(path, std::ios::trunc);
    for (auto id : installedApps)
    {
        file << id << std::endl;
    }
}

static std::vector<std::filesystem::path> getSteamLibraryPaths()
{
    std::vector<std::filesystem::path> paths;
    const char* home = getenv("HOME");
    if (!home) return paths;

    // Find Steam root
    std::filesystem::path steamRoot;
    for (auto& candidate : {
        std::filesystem::path(home) / ".steam" / "steam",
        std::filesystem::path(home) / ".local" / "share" / "Steam",
        std::filesystem::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
    }) {
        if (std::filesystem::exists(candidate / "steamapps")) {
            std::error_code ec;
            steamRoot = std::filesystem::canonical(candidate, ec);
            if (ec) steamRoot = candidate;
            break;
        }
    }

    if (steamRoot.empty()) return paths;

    // Parse libraryfolders.vdf for all "path" entries
    auto vdfPath = steamRoot / "steamapps" / "libraryfolders.vdf";
    if (!std::filesystem::exists(vdfPath)) {
        // Fallback: just use the default steamapps
        paths.push_back(steamRoot / "steamapps");
        return paths;
    }

    std::ifstream vdf(vdfPath);
    std::string line;
    while (std::getline(vdf, line))
    {
        if (line.find("\"path\"") == std::string::npos) continue;

        // Extract value: "path"		"/some/path"
        auto lastQuote = line.rfind('"');
        if (lastQuote == std::string::npos) continue;
        auto secondLastQuote = line.rfind('"', lastQuote - 1);
        if (secondLastQuote == std::string::npos) continue;

        std::string libPath = line.substr(secondLastQuote + 1, lastQuote - secondLastQuote - 1);
        auto steamappsPath = std::filesystem::path(libPath) / "steamapps";
        if (std::filesystem::exists(steamappsPath)) {
            paths.push_back(steamappsPath);
        }
    }

    if (paths.empty()) {
        paths.push_back(steamRoot / "steamapps");
    }

    return paths;
}

void Apps::deleteGameFiles(uint32_t appId)
{
    g_pLog->info("Apps::deleteGameFiles(%u)\n", appId);

    auto libraryPaths = getSteamLibraryPaths();
    std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";

    for (auto& libPath : libraryPaths)
    {
        auto manifestPath = libPath / manifestName;
        if (!std::filesystem::exists(manifestPath))
        {
            g_pLog->info("No manifest at %s, skipping\n", manifestPath.c_str());
            continue;
        }

        g_pLog->info("Found manifest: %s\n", manifestPath.c_str());

        // Parse installdir from the manifest
        std::ifstream manifest(manifestPath);
        std::string line;
        std::string installDir;
        while (std::getline(manifest, line))
        {
            if (line.find("\"installdir\"") == std::string::npos) continue;

            auto lastQuote = line.rfind('"');
            if (lastQuote == std::string::npos) continue;
            auto secondLastQuote = line.rfind('"', lastQuote - 1);
            if (secondLastQuote == std::string::npos) continue;

            installDir = line.substr(secondLastQuote + 1, lastQuote - secondLastQuote - 1);
            break;
        }
        manifest.close();

        // Delete the game directory
        if (!installDir.empty())
        {
            auto gamePath = libPath / "common" / installDir;
            if (std::filesystem::exists(gamePath))
            {
                g_pLog->info("Deleting game directory: %s\n", gamePath.c_str());
                std::error_code ec;
                std::filesystem::remove_all(gamePath, ec);
                if (ec)
                    g_pLog->warn("Failed to delete game directory: %s\n", ec.message().c_str());
                else
                    g_pLog->info("Successfully deleted game directory\n");
            }
        }
        else
        {
            g_pLog->warn("Could not parse installdir from manifest\n");
        }

        // Delete the appmanifest
        {
            std::error_code ec;
            std::filesystem::remove(manifestPath, ec);
            if (ec)
                g_pLog->warn("Failed to delete manifest: %s\n", ec.message().c_str());
            else
                g_pLog->info("Deleted manifest: %s\n", manifestPath.c_str());
        }
    }
}

bool Apps::gameFilesExist(uint32_t appId)
{
    g_pLog->info("Apps::gameFilesExist(%u)\n", appId);

    auto libraryPaths = getSteamLibraryPaths();
    std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";

    for (auto& libPath : libraryPaths)
    {
        auto manifestPath = libPath / manifestName;
        if (std::filesystem::exists(manifestPath))
        {
            g_pLog->info("Found manifest: %s\n", manifestPath.c_str());

            // Parse installdir from the manifest
            std::ifstream manifest(manifestPath);
            std::string line;
            std::string installDir;
            while (std::getline(manifest, line))
            {
                if (line.find("\"installdir\"") == std::string::npos) continue;

                auto lastQuote = line.rfind('"');
                if (lastQuote == std::string::npos) continue;
                auto secondLastQuote = line.rfind('"', lastQuote - 1);
                if (secondLastQuote == std::string::npos) continue;

                installDir = line.substr(secondLastQuote + 1, lastQuote - secondLastQuote - 1);
                break;
            }
            manifest.close();

            if (!installDir.empty())
            {
                auto gamePath = libPath / "common" / installDir;
                if (std::filesystem::exists(gamePath))
                {
                    g_pLog->info("Game directory exists: %s\n", gamePath.c_str());
                    return true;
                }
            }
        }
    }
    return false;
}

bool Apps::unlockApp(uint32_t appId, CAppOwnershipInfo* info, uint32_t ownerId)
{
	//Changing the purchased field is enough, but just for nicety in the Steamclient UI we change the owner too
	info->owner = ownerId;
	info->realOwner = 0;
	info->familyShared = ownerId != g_currentSteamId;

	info->licensePermanent = !info->familyShared;
	info->retailLicense = false;
	info->licenseExpired = false;
	info->licensePending = false;
	info->licenseLocked = false;

	info->releaseState = ERELEASESTATE_RELEASED;
	info->ownsLicense = true;

	info->lowViolence = false;
	info->regionRestricted = false;

	info->autoGrant = false;
	info->trialTime = 0;
	info->fromFreeWeekend = false;
	info->freeLicense = info->familyShared;
	info->siteLicense = false;

	g_pLog->once("Unlocked %u\n", appId);
	return true;
}

bool Apps::unlockApp(uint32_t appId, CAppOwnershipInfo* info)
{
	return unlockApp(appId, info, g_currentSteamId);
}

bool Apps::checkAppOwnership(uint32_t appId, CAppOwnershipInfo* pInfo)
{
	//Wait Until GetSubscribedApps gets called once to let Steam request and populate legit data first.
	//Afterwards modifying should hopefully not affect false positives anymore
	if (!applistRequested || !pInfo || !g_currentSteamId)
	{
		return false;
	}

	const uint32_t denuvoOwner = g_config.getDenuvoGameOwner(appId);

	//Do not modify Denuvo enabled Games
	if (denuvoOwner && denuvoOwner != g_currentSteamId)
	{
		//Would love to log the SteamId, but for users anonymity I won't
		g_pLog->once("Skipping %u because it's a Denuvo game from someone else\n", appId);
		return false;
	}

	if (g_config.shouldExcludeAppId(appId))
	{
		return false;
	}

	if (pInfo->lowViolence)
	{
		pInfo->lowViolence = false;
		g_pLog->once("Decensoring %u\n", appId);
	}
	if (pInfo->regionRestricted)
	{
		pInfo->regionRestricted = false;
		g_pLog->once("Bypassing region restriction for %u\n", appId);
	}

	const auto times = g_config.subscriptionTimestamps.get();
	if (times.contains(appId))
	{
		pInfo->purchaseTime = times.at(appId);
	}

	const bool manualUnlock = g_config.isAddedAppId(appId);
	if (!manualUnlock && (!g_config.playNotOwnedGames.get() || pInfo->ownsLicense))
	{
		return false;
	}

	// Do not unlock games that are already family shared.
	// If we unlock them, they lose their family share status in the UI,
	// which can break cloud saves and proper tracking.
	// (The family share lock is already bypassed in sendGamesPlayed)
	if (pInfo->familyShared)
	{
		g_pLog->once("checkAppOwnership(%u): Game is Family Shared, skipping unlock.\n", appId);
		return false;
	}

	if (!manualUnlock && g_config.automaticFilter.get())
	{
		//Returning false after we modify data shouldn't cause any problems because it should just get discarded
		if (!g_pClientApps)
		{
			return false;
		}

		auto type = g_pClientApps->getAppType(appId);
		if (type == APPTYPE_DLC) //Don't touch DLC here, otherwise downloads might break. Hopefully this won't decrease compatibility
		{
			return false;
		}

		switch(type)
		{
			case APPTYPE_APPLICATION:
			case APPTYPE_GAME:
				break;

			default:
				return false;
		}
	}

	unlockApp(appId, pInfo);

	return true;
}

void Apps::getSubscribedApps(uint32_t* appList, size_t size, uint32_t& count)
{
	//Valve calls this function twice, once with size of 0 then again
	if (!size || !appList)
	{
		g_pLog->info("getSubscribedApps: Steam requested app count. Original: %u, Added: %zu\n", count, g_config.addedAppIds.get().size());
		count = count + g_config.addedAppIds.get().size();
		return;
	}

	g_pLog->info("getSubscribedApps: Populating app list. Current count: %u\n", count);
	
	for(auto& appId : g_config.addedAppIds.get())
	{
		appList[count++] = appId;
	}

	applistRequested = true;
}

bool Apps::shouldDisableCloud(uint32_t appId)
{
	return !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

bool Apps::shouldDisableCDKey(uint32_t appId)
{
	return !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

bool Apps::shouldDisableUpdates(uint32_t appId)
{
	//Using AdditionalApps here aswell so users can manually block updates
	return g_config.isAddedAppId(appId) || !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

void Apps::sendGamesPlayed(CMsgClientGamesPlayed* msg)
{
	auto titles = g_config.gameTitles.get();
	bool owned = false;

	for(int i = 0; i < msg->games_played_size(); i++)
	{
		auto game = CMsgClientGamesPlayed_GamePlayed(msg->games_played(i));

		if (!game.game_id())
		{
			continue;
		}

		if(!owned && g_pSteamEngine->getUser(0)->isSubscribed(game.game_id()))
		{
			owned = true;
		}

		if (g_config.disableFamilyLock.get())
		{
			game.set_owner_id(1);
		}

		if (titles.contains(game.game_id()))
		{
			game.set_game_extra_info(titles[game.game_id()]);
		}
		else if (!owned || FakeAppIds::getFakeAppId(game.game_id()))
		{
			char name[256] {}; //No clue how long titles can get
			g_pClientApps->getAppData(game.game_id(), "common/name", name, sizeof(name));
			g_pLog->debug("AppName %s\n", name);
			game.set_game_extra_info(name);
		}

		msg->mutable_games_played(i)->ParseFromString(game.SerializeAsString());

		g_pLog->debug("Playing game %llu with flags %u & pid %u\n", game.game_id(), game.game_flags(), game.process_id());
	}

	if (owned || msg->games_played_size() > 0)
	{
		return;
	}

	const auto statusApp = g_config.idleStatus.get();
	if (statusApp.appId)
	{
		auto game = msg->add_games_played();
		game->set_game_id(statusApp.appId);
		game->set_game_extra_info(statusApp.title);
		game->set_game_flags(0);

		if (g_config.disableFamilyLock.get())
		{
			game->set_owner_id(1);
		}
		//game->set_game_flags(EGAMEFLAG_MULTIPLAYER);
	}
}

void Apps::sendPICSInfoRequest(CMsgClientPICSProductInfoRequest* msg)
{
	const auto tokens = g_config.appTokens.get();

	for(int i = 0; i < msg->apps_size(); i++)
	{
		auto app = msg->mutable_apps(i);
		if (tokens.contains(app->appid()))
		{
			app->set_access_token(tokens.at(app->appid()));
			g_pLog->debug("Used access token from config for %u\n", app->appid());
		}
	}
}

void Apps::sendMsg(CProtoBufMsgBase *msg)
{
	switch(msg->type)
	{
		case EMSG_PICS_PRODUCTINFO_REQUEST:
			sendPICSInfoRequest(msg->getBody<CMsgClientPICSProductInfoRequest>());
			break;

		case EMSG_GAMESPLAYED:
		case EMSG_GAMESPLAYED_NO_DATABLOB:
		case EMSG_GAMESPLAYED_WITH_DATABLOB:
			sendGamesPlayed(msg->getBody<CMsgClientGamesPlayed>());
			break;
	}
}
