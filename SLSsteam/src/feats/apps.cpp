#include "apps.hpp"

#include "../config.hpp"
#include "../globals.hpp"
#include "../utils.hpp"

#include "fakeappid.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>



bool Apps::applistRequested;
std::unordered_set<AppId_t> Apps::privateApps = std::unordered_set<AppId_t>();

std::mutex Apps::pendingLicenseChangesMutex;
std::unordered_set<AppId_t> Apps::pendingLicenseChanges = std::unordered_set<AppId_t>();

std::map<uint32_t, int> Apps::appIdOwnerOverride;
std::set<uint32_t> Apps::installedApps;
std::set<uint32_t> Apps::onlineFixApps;
std::set<uint32_t> Apps::autoCrackApps;

static std::filesystem::path getAppsJsonPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::filesystem::path(home) / ".SLSsteam.json";
}

static void saveAppsJson()
{
    auto path = getAppsJsonPath();
    if (path.empty()) return;

    std::ofstream file(path, std::ios::trunc);
    file << "{\n";
    file << "  \"installed\": [";
    bool first = true;
    for (auto id : Apps::installedApps)
    {
        if (!first) file << ", ";
        file << id;
        first = false;
    }
    file << "],\n";
    file << "  \"onlinefix\": [";
    first = true;
    for (auto id : Apps::onlineFixApps)
    {
        if (!first) file << ", ";
        file << id;
        first = false;
    }
    file << "],\n";
    file << "  \"autocrack\": [";
    first = true;
    for (auto id : Apps::autoCrackApps)
    {
        if (!first) file << ", ";
        file << id;
        first = false;
    }
    file << "]\n";
    file << "}\n";
}

static void parseJsonArray(const std::string& content, const std::string& key, std::set<uint32_t>& target)
{
    target.clear();
    size_t keyPos = content.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return;
    size_t startBracket = content.find("[", keyPos);
    if (startBracket == std::string::npos) return;
    size_t endBracket = content.find("]", startBracket);
    if (endBracket == std::string::npos) return;
    
    std::string arrayStr = content.substr(startBracket + 1, endBracket - startBracket - 1);
    std::stringstream ss(arrayStr);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        size_t first = token.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = token.find_last_not_of(" \t\r\n");
        std::string valStr = token.substr(first, (last - first + 1));
        if (!valStr.empty())
        {
            try {
                target.insert(std::stoul(valStr));
            } catch (...) {}
        }
    }
}

// Keep the old path functions temporarily for migration
static std::filesystem::path getInstalledAppsPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::filesystem::path(home) / ".SLSsteam.installed";
}

static std::filesystem::path getOnlineFixAppsPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::filesystem::path(home) / ".SLSsteam.onlinefix";
}

static std::filesystem::path getAutoCrackAppsPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::filesystem::path(home) / ".SLSsteam.autocrack";
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

static void reloadOnlineFixApps()
{
    auto path = getOnlineFixAppsPath();
    if (path.empty() || !std::filesystem::exists(path)) return;

    std::ifstream file(path);
    uint32_t id;
    while (file >> id)
    {
        Apps::onlineFixApps.insert(id);
    }
}

static void reloadAutoCrackApps()
{
    auto path = getAutoCrackAppsPath();
    if (path.empty() || !std::filesystem::exists(path)) return;

    std::ifstream file(path);
    uint32_t id;
    while (file >> id)
    {
        Apps::autoCrackApps.insert(id);
    }
}

static void reloadAppsJson()
{
    auto path = getAppsJsonPath();
    if (path.empty()) return;

    if (!std::filesystem::exists(path))
    {
        // Migrate old files if present
        reloadInstalledApps();
        reloadOnlineFixApps();
        reloadAutoCrackApps();
        
        saveAppsJson();

        // Delete old files after migration
        auto oldInst = getInstalledAppsPath();
        if (!oldInst.empty() && std::filesystem::exists(oldInst)) std::filesystem::remove(oldInst);
        auto oldFix = getOnlineFixAppsPath();
        if (!oldFix.empty() && std::filesystem::exists(oldFix)) std::filesystem::remove(oldFix);
        auto oldCrack = getAutoCrackAppsPath();
        if (!oldCrack.empty() && std::filesystem::exists(oldCrack)) std::filesystem::remove(oldCrack);
        return;
    }

    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    parseJsonArray(content, "installed", Apps::installedApps);
    parseJsonArray(content, "onlinefix", Apps::onlineFixApps);
    parseJsonArray(content, "autocrack", Apps::autoCrackApps);
}

void Apps::init()
{
    reloadAppsJson();
}

bool Apps::isInstalled(uint32_t appId)
{
    return installedApps.contains(appId);
}

void Apps::setInstalled(uint32_t appId)
{
    LOG_INFO("Apps::setInstalled(%u)\n", appId);
    installedApps.insert(appId);
    saveAppsJson();
}

void Apps::removeInstalled(uint32_t appId)
{
    LOG_INFO("Apps::removeInstalled(%u)\n", appId);
    installedApps.erase(appId);
    saveAppsJson();
}

bool Apps::isOnlineFixInstalled(uint32_t appId)
{
    return onlineFixApps.contains(appId);
}

void Apps::setOnlineFixInstalled(uint32_t appId, bool installed)
{
    LOG_INFO("Apps::setOnlineFixInstalled(%u, %d)\n", appId, installed);
    if (installed) {
        onlineFixApps.insert(appId);
    } else {
        onlineFixApps.erase(appId);
    }
    saveAppsJson();
}

bool Apps::isAutoCrackInstalled(uint32_t appId)
{
    return autoCrackApps.contains(appId);
}

void Apps::setAutoCrackInstalled(uint32_t appId, bool installed)
{
    LOG_INFO("Apps::setAutoCrackInstalled(%u, %d)\n", appId, installed);
    if (installed) {
        autoCrackApps.insert(appId);
    } else {
        autoCrackApps.erase(appId);
    }
    saveAppsJson();
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
    LOG_INFO("Apps::deleteGameFiles(%u)\n", appId);

    auto libraryPaths = getSteamLibraryPaths();
    std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";

    for (auto& libPath : libraryPaths)
    {
        auto manifestPath = libPath / manifestName;
        if (!std::filesystem::exists(manifestPath))
        {
            LOG_INFO("No manifest at %s, skipping\n", manifestPath.c_str());
            continue;
        }

        LOG_INFO("Found manifest: %s\n", manifestPath.c_str());

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
                LOG_INFO("Deleting game directory: %s\n", gamePath.c_str());
                std::error_code ec;
                std::filesystem::remove_all(gamePath, ec);
                if (ec)
                    LOG_WARN("Failed to delete game directory: %s\n", ec.message().c_str());
                else
                    LOG_INFO("Successfully deleted game directory\n");
            }
        }
        else
        {
            LOG_WARN("Could not parse installdir from manifest\n");
        }

        // Delete the appmanifest
        {
            std::error_code ec;
            std::filesystem::remove(manifestPath, ec);
            if (ec)
                LOG_WARN("Failed to delete manifest: %s\n", ec.message().c_str());
            else
                LOG_INFO("Deleted manifest: %s\n", manifestPath.c_str());
        }
    }
}

bool Apps::gameFilesExist(uint32_t appId)
{
    LOG_INFO("Apps::gameFilesExist(%u)\n", appId);

    auto libraryPaths = getSteamLibraryPaths();
    std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";

    for (auto& libPath : libraryPaths)
    {
        auto manifestPath = libPath / manifestName;
        if (std::filesystem::exists(manifestPath))
        {
            LOG_INFO("Found manifest: %s\n", manifestPath.c_str());

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
                    LOG_INFO("Game directory exists: %s\n", gamePath.c_str());
                    return true;
                }
            }
        }
    }
    return false;
}

bool Apps::unlockApp(const AppId_t appId, AppOwnershipInfo_t* info, const CSteamId& ownerId)
{
	//Changing the purchased field is enough, but just for nicety in the Steamclient UI we change the owner too
	info->owner = ownerId.accountId();
	info->realOwner = 0;
	info->familyShared = info->owner != g_currentSteamId.accountId();

	info->licensePermanent = !info->familyShared;
	info->retailLicense = false;
	info->licenseExpired = false;
	info->licensePending = false;
	info->licenseLocked = false;

	info->releaseState = k_EAppReleaseStateReleased;
	info->ownsLicense = true;

	info->lowViolence = false;
	info->regionRestricted = false;

	info->autoGrant = false;
	info->trialTime = 0;
	info->fromFreeWeekend = false;
	info->freeLicense = info->familyShared;
	info->siteLicense = false;

	LOG_ONCE("Unlocked %u\n", appId);
	return true;
}

bool Apps::unlockApp(const AppId_t appId, AppOwnershipInfo_t* info)
{
	return unlockApp(appId, info, g_currentSteamId);
}

void Apps::buildDepotDependency(CUtlVector<DepotInfo_t>* depots, CUtlVector<DepotInfo_t>* sharedDepots)
{
	LOG_DEBUG("Vec Alloc %u, Grow %u, Size %u\n", depots->mem.alloc, depots->mem.growSize, depots->size);

	const auto depotBlacklist = g_config.depotBlacklist.get();
	const auto manifestOverrides = g_config.manifestIds.get();

	for (unsigned int i = 0; i < depots->size; i++)
	{
		const auto depot = depots->at(i);

		if (depotBlacklist.contains(depot->depotId))
		{
			LOG_DEBUG("Removing %u with %llu\n", depot->depotId, depot->manifestId);
			depots->swap(i, depots->size - 1);
			depots->size--;
		}

		if (manifestOverrides.contains(depot->depotId))
		{
			const uint64_t oldId = depot->manifestId;
			depot->manifestId = manifestOverrides.at(depot->depotId);
			LOG_DEBUG("Overrode %u's manifest %llu with %llu\n", depot->depotId, oldId, depot->manifestId);
		}

		LOG_DEBUG("Depot %u for %u -> %llu\n", depot->depotId, depot->appId, depot->manifestId);
	}

	for (unsigned int i = 0; i < sharedDepots->size; i++)
	{
		const auto depot = sharedDepots->at(i);
		LOG_DEBUG("Shared Depot %u for %u -> %llu\n", depot->depotId, depot->appId, depot->manifestId);
	}
}

bool Apps::checkAppOwnership(AppId_t appId, AppOwnershipInfo_t* pInfo)
{
	//Wait Until GetSubscribedApps gets called once to let Steam request and populate legit data first.
	//Afterwards modifying should hopefully not affect false positives anymore
	if (!applistRequested || !pInfo || !g_currentSteamId.isSet())
	{
		return false;
	}

	const CSteamId denuvoOwner = g_config.getDenuvoGameOwner(appId);

	//Do not modify Denuvo enabled Games
	if (denuvoOwner.isSet() && denuvoOwner.steamId64 != g_currentSteamId.steamId64)
	{
		//Would love to log the SteamId, but for users anonymity I won't
		LOG_ONCE("Skipping %u because it's a Denuvo game from someone else\n", appId);
		return false;
	}

	if (g_config.shouldExcludeAppId(appId))
	{
		return false;
	}

	if (pInfo->lowViolence)
	{
		pInfo->lowViolence = false;
		LOG_ONCE("Decensoring %u\n", appId);
	}
	if (pInfo->regionRestricted)
	{
		pInfo->regionRestricted = false;
		LOG_ONCE("Bypassing region restriction for %u\n", appId);
	}

	const auto times = g_config.subscriptionTimestamps.get();
	if (times.contains(appId))
	{
		pInfo->purchaseTime = times.at(appId);
	}

	if (!g_config.isAddedAppId(appId))
	{
		return false;
	}

	// Do not unlock games that are already family shared.
	// If we unlock them, they lose their family share status in the UI,
	// which can break cloud saves and proper tracking.
	// (The family share lock is already bypassed in sendGamesPlayed)
	if (pInfo->familyShared)
	{
		LOG_ONCE("checkAppOwnership(%u): Game is Family Shared, skipping unlock.\n", appId);
		return false;
	}

	unlockApp(appId, pInfo);

	return true;
}

void Apps::getLegacyCDKey(const AppId_t appId)
{
	const auto user = g_pSteamEngine->getUser();
	if (user->isSubscribed(appId))
	{
		return;
	}

	std::string newKey;

	const auto keys = g_config.cdKeys.get();
	if (keys.contains(appId))
	{
		newKey = keys.at(appId);
		LOG_DEBUG("Using key from config for %u\n", appId);
	}
	else
	{
		constexpr const char* CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		static const unsigned int CHARS_SIZE = strlen(CHARS);

		//Some games use 5 SEGMENT_CHARS/5 SEGMENT_NUM or a mix of both
		//4 * 4 is the most common one though
		constexpr unsigned int SEGMENT_CHARS = 4;
		constexpr unsigned int SEGMENT_NUM = 4;

		constexpr unsigned int SEGMENT_SIZE = SEGMENT_CHARS + 1; //AAAA-, BBBB-, etc
		constexpr unsigned int KEY_SIZE = SEGMENT_SIZE * SEGMENT_NUM - 1; //Do not end with -
		
		//Don't forget null terminator since we
		//do not pass a size argument to SetLegacyCDKey
		newKey.resize(KEY_SIZE);

		srand(g_currentSteamId.steamId.accountId + appId);

		for (unsigned int i = 0; i < KEY_SIZE; i++)
		{
			if ((i + 1) % SEGMENT_SIZE == 0)
			{
				newKey[i] = '-';
				continue;
			}

			const unsigned int num = rand() % CHARS_SIZE;
			newKey[i] = CHARS[num];
		}

		LOG_DEBUG("Generated random key %s for %u\n", newKey.c_str(), appId);
	}

	const auto clientUser = user->getClientUser();

	//Wrapper function for CUser::SetLegacyCDKey, which gets called
	//from CCMInterface when a legacy cd key packet arrives
	//Injecting them only in GetLegacyCDKey doesn't work right, so we set it instead
	if (!clientUser->setLegacyCDKey(appId, newKey.c_str()))
	{
		LOG_ERROR("Failed to set CDKey for %u\n", appId);
	}
}

void Apps::getSubscribedApps(AppId_t* appList, const uint32_t size, uint32_t& count)
{
	//Valve calls this function twice, once with size of 0 then again
	if (!size || !appList)
	{
		count = count + g_config.addedAppIds.get().size();
		return;
	}

	//TODO: Maybe Add check if AppId already in list before blindly appending
	for (auto& appId : g_config.addedAppIds.get())
	{
		appList[count++] = appId;
	}

	applistRequested = true;
}

void Apps::parseProductInfoFromResponse(CMsgClientPICSProductInfoResponse* msg)
{
	std::lock_guard lock(pendingLicenseChangesMutex);

	auto set = std::unordered_set<AppId_t>();
	for (const auto& app : msg->apps())
	{
		if (!pendingLicenseChanges.contains(app.appid()))
		{
			continue;
		}

		set.emplace(app.appid());
		pendingLicenseChanges.erase(app.appid());
	}

	postAppLicensesChanged(set);
}

void Apps::postAppLicensesChanged(const std::unordered_set<AppId_t>& apps)
{
	if (!apps.size())
	{
		return;
	}

	const auto user = g_pSteamEngine->getUser(0);
	if (!user)
	{
		return;
	}

	AppLicensesChanged_t cb { };
	unsigned int totalPackets = std::floor(apps.size() / AppLicensesChanged_t::MAX_APPS_PER_CALLBACK);

	for (unsigned int i = 0; i < apps.size(); i++)
	{
		unsigned int idx = i % AppLicensesChanged_t::MAX_APPS_PER_CALLBACK;

		cb.apps[idx] = *std::next(apps.begin(), i);
		cb.count = idx + 1;
		cb.appsAdded |= 1llu << idx;
		cb.remainingPackets = totalPackets;

		LOG_DEBUG("AppLicensesChanged_t.apps[%u] -> %u (i -> %i, packets left -> %i, appsAdded %llu)\n", idx, cb.apps[idx], i, totalPackets, cb.appsAdded);

		if (idx + 1 >= AppLicensesChanged_t::MAX_APPS_PER_CALLBACK)
		{
			user->postCallback(ECallbackType::AppLicensesChanged_t, &cb, sizeof(cb));
			totalPackets--;
			memset(&cb, 0, sizeof(cb));
		}
	}

	if (cb.count)
	{
		user->postCallback(ECallbackType::AppLicensesChanged_t, &cb, sizeof(cb));
	}

	std::ostringstream appsLog;
	for (const auto& app : apps)
	{
		appsLog << (appsLog.str().size() ? ", " : "") << app;
	}

	LOG_API("AppLicensesChanged callback invoked for %s!\n", appsLog.str().c_str());
}

void Apps::runIPCFrame()
{
	const auto usr = g_pSteamEngine->getUser();
	if (!usr)
	{
		return;
	}

	const std::lock_guard appsChanged(g_config.appsChangedMutex);
	const auto appInfo = usr->getClientApps();

	if (g_config.removedApps.size())
	{
		postAppLicensesChanged(g_config.removedApps);
		g_config.removedApps.clear();
	}

	const auto added = g_config.newApps;

	if (!added.size())
	{
		return;
	}

	const std::lock_guard pendingLicensesLock(pendingLicenseChangesMutex);

	//Max batch of 15, otherwise not all apps will get a response which means they won't get added
	constexpr unsigned int MAX_APPS_PER_REQUEST = 15;
	AppId_t apps[MAX_APPS_PER_REQUEST] { };

	unsigned int i = 0;
	for (; i < added.size(); i++)
	{
		const unsigned int idx = i % MAX_APPS_PER_REQUEST;
		const AppId_t appId = *std::next(added.begin(), i);

		apps[idx] = appId;

		LOG_DEBUG("AppInfoRequest %u -> %u from (%i)\n", idx, apps[idx], i);

		if (idx + 1 >= MAX_APPS_PER_REQUEST)
		{
			appInfo->requestAppInfoUpdate(apps, MAX_APPS_PER_REQUEST);
			memset(apps, 0, sizeof(apps));
		}

		pendingLicenseChanges.emplace(appId);
	}

	const unsigned int idx = i % MAX_APPS_PER_REQUEST;
	if (apps[0])
	{
		appInfo->requestAppInfoUpdate(apps, idx);
	}

	g_config.newApps.clear();
}

bool Apps::shouldDisableCloud(const AppId_t appId)
{
	if (!g_config.disableCloud.get())
	{
		return false;
	}

	return !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

bool Apps::shouldDisableCDKey(const AppId_t appId)
{
	return !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

bool Apps::shouldDisableUpdates(const AppId_t appId)
{
	if (!g_config.disableUpdates.get())
	{
		return false;
	}

	//Using AdditionalApps here aswell so users can manually block updates
	return g_config.isAddedAppId(appId) || !g_pSteamEngine->getUser(0)->isSubscribed(appId);
}

void Apps::sendAndRecvLastPlayedTimes(const char* name, CPlayer_GetLastPlayedTimes_Response* recv)
{
	if (strcmp(name, "Player.ClientGetLastPlayedTimes#1") != 0)
	{
		return;
	}

	const auto apps = g_config.addedAppIds.get();
	for (int i = recv->games_size() - 1; i >= 0; i--)
	{
		auto game = recv->mutable_games(i);
		if (!apps.contains(game->appid()))
		{
			continue;
		}

		LOG_DEBUG("Removed serverside PlayTime for %u\n", game->appid());
		recv->mutable_games()->DeleteSubrange(i, 1);
	}
}

void Apps::sendGamesPlayed(CNetPacket* pkt)
{
	const auto titles = g_config.gameTitles.get();
	const auto usr = g_pSteamEngine->getUser();
	const auto appInfo = usr->getClientApps();

	auto msg = pkt->deserializeBody<CMsgClientGamesPlayed>();

	for (int i = 0; i < msg.games_played_size(); i++)
	{
		auto game = msg.mutable_games_played(i);
		if (!game->game_id())
		{
			continue;
		}

		const uint64_t gameId = game->game_id();

		// Native non-Steam shortcut IDs use 0x2000000 in their low 32 bits.
		// Leave the original shortcut title and 64-bit ID untouched.
		if (gameId & GAME_TYPE_SHORTCUT)
		{
			LOG_DEBUG("Preserving non-Steam shortcut %llu\n", gameId);
			continue;
		}

		if (g_config.disableFamilyLock.get())
		{
			game->set_owner_id(1);
		}

		if (titles.contains(gameId))
		{
			game->set_game_extra_info(titles.at(gameId));
		}
		//This probably belongs into FakeAppIds, but the GameTitles does not so it stays here
		else if (FakeAppIds::getFakeAppId(gameId))
		{
			char name[256] {}; //No clue how long titles can get
			int len;

			if (privateApps.contains(gameId))
			{
				strcpy(name, "Redacted");
				len = strlen(name);
			}
			else
			{
				len = appInfo->getAppData(gameId, "common/name", name, sizeof(name));
			}

			if (len > 0)
			{
				LOG_DEBUG("AppName %s (%i)\n", name, len);
				game->set_game_extra_info(name);
			}
		}

		//msg->mutable_games_played(i)->ParseFromString(game.SerializeAsString());

		LOG_DEBUG("Playing game %llu with flags %u & pid %u\n", gameId, game->game_flags(), game->process_id());
	}

	if (msg.games_played_size() < 1)
	{
		const auto statusApp = g_config.idleStatus.get();
		if (statusApp.appId)
		{
			auto game = msg.add_games_played();
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

	pkt->serialize(msg);
}

void Apps::sendPICSInfoRequest(CNetPacket* pkt)
{
	const auto tokens = g_config.appTokens.get();
	auto msg = pkt->deserializeBody<CMsgClientPICSProductInfoRequest>();

	for (int i = 0; i < msg.apps_size(); i++)
	{
		auto app = msg.mutable_apps(i);
		if (tokens.contains(app->appid()))
		{
			app->set_access_token(tokens.at(app->appid()));
			LOG_DEBUG("Used access token from config for %u\n", app->appid());
		}
	}

	pkt->serialize(msg);
}

void Apps::sendMsg(CNetPacket *pkt)
{
	switch(pkt->getProtoBufType())
	{
		case k_EMsgClientPICSProductInfoRequest:
			sendPICSInfoRequest(pkt);
			break;

		case k_EMsgClientGamesPlayed:
		case k_EMsgClientGamesPlayedNoDataBlob:
		case k_EMsgClientGamesPlayedWithDataBlob:
			sendGamesPlayed(pkt);
			break;

		default:
			break;
	}
}

void Apps::setConfigStoreString(const char* key, const char* value)
{
	if (!std::string(key).starts_with("WebStorage\\PrivateApps"))
	{
		return;
	}

	LOG_DEBUG("%s -> %s\n", key, value);

	auto str = std::string(value);
	if (str.size() < 3) //List is empty, nope out
	{
		return;
	}

	privateApps.clear();
	str = str.substr(1, str.size() - 2); //[730,240,440,etc]
	const auto split = Utils::strsplit(const_cast<char*>(str.c_str()), ",");

	for (const auto& s : split)
	{
		if (!Utils::isNumber(s.c_str()))
		{
			LOG_WARN("%s is not a number! Skipping\n", s.c_str());
		}

		const AppId_t appId = std::stoul(s);
		privateApps.emplace(appId);
		LOG_DEBUG("Added %u to privateApps\n", appId);
	}
}
