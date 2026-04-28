#include "config.hpp"

#include "config_default.hpp"
#include "filewatcher.hpp"
#include "log.hpp"
#include "yaml-cpp/yaml.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <sys/inotify.h>
#include <vector>


std::string CConfig::getDir()
{
	char pathBuf[255];
	const char* configDir = getenv("XDG_CONFIG_HOME"); //Most users should have this set iirc
	if (configDir != NULL)
	{
		sprintf(pathBuf, "%s/SLSsteam", configDir);
	}
	else
	{
		const char* home = getenv("HOME");
		sprintf(pathBuf, "%s/.config/SLSsteam", home);
	}

	return std::string(pathBuf);
}

std::string CConfig::getPath()
{
	return getDir().append("/config.yaml");
}

std::string CConfig::getPluginDir()
{
	const char* home = std::getenv("HOME");
	if (!home) return "";

	const std::string candidatePaths[] = {
		std::string(home) + "/.steam/steam",
		std::string(home) + "/.local/share/Steam"
	};

	for (const auto& candidate : candidatePaths)
	{
		if (std::filesystem::exists(candidate))
		{
			return candidate + "/config/stplug-in";
		}
	}
	return "";
}

bool CConfig::createFile()
{
	std::string path = getPath();
	if (!std::filesystem::exists(path))
	{
		std::string dir = getDir();
		if (!std::filesystem::exists(dir))
		{
			if (!std::filesystem::create_directory(dir))
			{
				g_pLog->info("Unable to create config directory at %s!\n", dir.c_str());
				return false;
			}

			g_pLog->debug("Created config directory at %s\n", dir.c_str());
		}

		FILE* file = fopen(path.c_str(), "w");
		if (!file)
		{
			g_pLog->info("Unable to create config at %s!\n", path.c_str());
			return false;
		}

		fputs(defaultConfig, file);
		fflush(file);
		fclose(file);
	}

	return true;
}

static void onFileChange(const char* filename)
{
	if (filename)
	{
		std::string s(filename);
		if (!s.ends_with(".lua") && !s.ends_with(".manifest") && s != "config.yaml")
		{
			return;
		}
		g_pLog->debug("onFileChange triggered by %s\n", filename);
	}

	g_config.loadSettings();
	scanLuaPluginsAndUpdateConfig();
}

bool CConfig::init()
{
	if(createFile())
	{
		watcher = new CFileWatcher(onFileChange);
		watcher->addWatch(getPath().c_str());

		std::string pluginDir = getPluginDir();
		if (!pluginDir.empty())
		{
			if (!std::filesystem::exists(pluginDir))
			{
				std::filesystem::create_directories(pluginDir);
			}
			// IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM
			watcher->addWatch(pluginDir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
		}

		watcher->start();
	}

	loadSettings();
	return true;
}

CConfig::~CConfig()
{
	if (watcher)
	{
		delete watcher;
	}
}


void CConfig::setError(ELoadError err)
{
	if (__loadErrors.get() > err)
	{
		return;
	}

	__loadErrors = err;
}

bool CConfig::loadSettings()
{
	YAML::Node node;
	try
	{
		node = YAML::LoadFile(getPath());
	}
	catch (YAML::BadFile& bf)
	{
		g_pLog->info("Can not read config.yaml! %s\nUsing defaults", bf.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	catch (YAML::ParserException& pe)
	{
		g_pLog->info("Error parsing config.yaml! %s\nUsing defaults", pe.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}

	__loadErrors = ELoadError::None;
	
	disableFamilyLock = getSetting<bool>(node, "DisableFamilyShareLock", true);
	useWhiteList = getSetting<bool>(node, "UseWhitelist", false);
	automaticFilter = getSetting<bool>(node, "AutoFilterList", true);
	playNotOwnedGames = getSetting<bool>(node, "PlayNotOwnedGames", false);
	safeMode = getSetting<bool>(node, "SafeMode", false);
	notifications = getSetting<bool>(node, "Notifications", true);
	warnHashMissmatch = getSetting<bool>(node, "WarnHashMissmatch", false);
	notifyInit = getSetting<bool>(node, "NotifyInit", true);
	api = getSetting<bool>(node, "API", true);
	fakeEmail = getSetting<std::string>(node, "FakeEmail", "");
	fakeWalletBalance = getSetting<int32_t>(node, "FakeWalletBalance", 0);
	extendedLogging = getSetting<bool>(node, "ExtendedLogging", false);
	logLevel = getSetting<unsigned int>(node, "LogLevel", 2);

	//TODO: Create smart logging function to log them automatically via getSetting
	g_pLog->info("DisableFamilyShareLock: %i\n", disableFamilyLock.get());
	g_pLog->info("UseWhitelist: %i\n", useWhiteList.get());
	g_pLog->info("AutoFilterList: %i\n", automaticFilter.get());
	g_pLog->info("PlayNotOwnedGames: %i\n", playNotOwnedGames.get());
	g_pLog->info("SafeMode: %i\n", safeMode.get());
	g_pLog->info("Notifications: %i\n", notifications.get());
	g_pLog->info("WarnHashMissmatch: %i\n", warnHashMissmatch.get());
	g_pLog->info("NotifyInit: %i\n", notifyInit.get());
	g_pLog->info("API: %i\n", api.get());
	g_pLog->info("FakeEmail: %s\n", fakeEmail.get().c_str());
	g_pLog->info("FakeWalletBalance: %i\n", fakeWalletBalance.get());
	g_pLog->info("ExtendedLogging: %i\n", extendedLogging.get());
	g_pLog->info("LogLevel: %i\n", logLevel.get());

	appIds = getList<uint32_t>(node, "AppIds");
	addedAppIds = getList<uint32_t>(node, "AdditionalApps");
	fakeOffline = getList<uint32_t>(node, "FakeOffline");

	fakeAppIds = getMap<uint32_t, uint32_t>(node, "FakeAppIds");
	appTokens = getMap<uint32_t, uint64_t>(node, "AppTokens");
	gameTitles = getMap<uint32_t, std::string>(node, "GameTitles");
	subscriptionTimestamps = getMap<uint32_t, uint32_t>(node, "SubscriptionTimestamps");

	//Do not warn for these (yet?)
	const auto idleStatusNode = node["IdleStatus"];
	if (idleStatusNode)
	{
		try
		{
			auto appId = idleStatusNode["AppId"].as<uint32_t>();
			auto title = idleStatusNode["Title"].as<std::string>();

			idleStatus = FakeGame_t
			{
				appId,
				title
			};

			g_pLog->info("Idle status %s with AppId %u\n", title.c_str(), appId);
		}
		catch(...)
		{
			//g_pLog->warn("Failed to parse IdleStatus!");A
			setError(ELoadError::ParsingException);
		}
	}
	const auto unownedStatusNode = node["UnownedStatus"];
	if (unownedStatusNode)
	{
		try
		{
			auto appId = unownedStatusNode["AppId"].as<uint32_t>();
			auto title = unownedStatusNode["Title"].as<std::string>();

			unownedStatus = FakeGame_t
			{
				appId,
				title
			};

			g_pLog->info("Unowned status %s with AppId %u\n", title.c_str(), appId);
		}
		catch(...)
		{
			//g_pLog->warn("Failed to parse UnownedStatus");
			setError(ELoadError::ParsingException);
		}
	}

	const auto dlcDataNode = node["DlcData"];
	if(dlcDataNode)
	{
		auto _dlcData = dlcData.empty();

		for(auto& app : dlcDataNode)
		{
			try
			{
				const uint32_t parentId = app.first.as<uint32_t>();

				CDlcData data;
				data.parentId = parentId;
				g_pLog->info("Adding DlcData for %u\n", parentId);

				for(auto& dlc : app.second)
				{
					const uint32_t dlcId = dlc.first.as<uint32_t>();
					//There's more efficient types to store strings, but they mostly do not work
					const std::string dlcName = dlc.second.as<std::string>();

					data.dlcIds[dlcId] = dlcName;
					g_pLog->info("DlcId %u -> %s\n", dlcId, dlcName.c_str());
				}

				_dlcData[parentId] = data;
			}
			catch(...)
			{
				//g_pLog->info("Failed to parse DlcData!");
				setError(ELoadError::ParsingException);
				break;
			}
		}

		dlcData = _dlcData;
	}
	else
	{
		//g_pLog->info("Missing DlcData entry in config!");
		setError(ELoadError::MissingKey);
	}

	const auto denuvoGamesNode = node["DenuvoGames"];
	if (denuvoGamesNode)
	{
		auto _denuvoGames = denuvoGames.empty();

		for (auto& steamIdNode : denuvoGamesNode)
		{
			try
			{
				const uint32_t steamId = steamIdNode.first.as<uint32_t>();
				_denuvoGames[steamId] = std::unordered_set<uint32_t>();

				for (auto& appIdNode : steamIdNode.second)
				{
					const uint32_t appId = appIdNode.as<uint32_t>();
					_denuvoGames[steamId].emplace(appId);

					//Again, not loggin SteamId because of privacy
					g_pLog->info("Added DenuvoGame %u\n", appId);
				}
			}
			catch (...)
			{
				//g_pLog->info("Failed to parse DenuvoGames!");
				setError(ELoadError::ParsingException);
			}
		}

		denuvoGames.set(_denuvoGames);
	}
	else
	{
		//g_pLog->info("Missing DenuvoGames entry in config!");
		setError(ELoadError::MissingKey);
	}

	switch(__loadErrors.get())
	{
		case ELoadError::MissingKey:
			g_pLog->info("Issues during config loading encountered! Missing key(s)");
			break;
		case ELoadError::ParsingException:
			g_pLog->info("Issues during config loading encountered! Parsing error(s)");
			break;

		default:
			break;
	}


	return true;
}

bool CConfig::isAddedAppId(uint32_t appId)
{
	return addedAppIds.get().contains(appId);
}

bool CConfig::shouldExcludeAppId(uint32_t appId)
{
	bool exclude = false;
	//Proper way would be with getAppType, but that seems broken so we need to do this instead
	constexpr uint32_t ONE_BILLION = 1E9; //Implicit cast from double to unsigned int, hopefully this does not break anything
	if (appId >= ONE_BILLION) //Higher and equal to 10^9 gets used by Steam Internally
	{
		exclude = true;
	}
	else
	{
		bool found = appIds.get().contains(appId);
		exclude = !isAddedAppId(appId) && ((useWhiteList.get() && !found) || (!useWhiteList.get() && found));
	}

	g_pLog->once("shouldExcludeAppId(%u) -> %i\n", appId, exclude);
	return exclude;
}

uint32_t CConfig::getDenuvoGameOwner(uint32_t appId)
{
	for(const auto& tpl : denuvoGames.get())
	{
		if (tpl.second.contains(appId))
		{
			//g_pLog->once("%u is DenuvoGame\n", appId);
			return tpl.first;
		}
	}

	return 0;
}

bool CConfig::addAdditionalAppId(uint32_t appId)
{
	const std::string configPath = getPath();

	// 1. Read config.yaml line by line
	std::ifstream inFile(configPath);
	if (!inFile.is_open())
	{
		g_pLog->info("addAdditionalAppId: Cannot open config at %s\n", configPath.c_str());
		return false;
	}

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(inFile, line))
	{
		lines.push_back(line);
	}
	inFile.close();

	// 2. Check if appId already exists under AdditionalApps (avoid duplicates)
	bool inAdditionalApps = false;
	for (const auto& l : lines)
	{
		if (l.find("AdditionalApps:") != std::string::npos
			&& !l.empty() && l[0] != '#' && l[0] != ' ' && l[0] != '\t')
		{
			inAdditionalApps = true;
			continue;
		}

		if (inAdditionalApps)
		{
			if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
			{
				inAdditionalApps = false;
				continue;
			}

			size_t dashPos = l.find("- ");
			if (dashPos != std::string::npos)
			{
				bool leadingWS = true;
				for (size_t i = 0; i < dashPos; ++i)
				{
					if (l[i] != ' ' && l[i] != '\t') { leadingWS = false; break; }
				}
				if (leadingWS)
				{
					std::string val = l.substr(dashPos + 2);
					size_t cmt = val.find('#');
					if (cmt != std::string::npos) val = val.substr(0, cmt);
					while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
					try
					{
						if (static_cast<uint32_t>(std::stoul(val)) == appId)
						{
							g_pLog->info("addAdditionalAppId: AppID %u already in AdditionalApps\n", appId);
							return true;
						}
					}
					catch (...) {}
				}
			}
		}
	}

	// 3. Find the AdditionalApps: section header and the insertion point
	int sectionHeaderIdx = -1;
	int lastEntryIdx = -1;
	inAdditionalApps = false;

	for (int i = 0; i < static_cast<int>(lines.size()); ++i)
	{
		const auto& l = lines[i];

		if (!inAdditionalApps)
		{
			if (l.find("AdditionalApps:") != std::string::npos
				&& !l.empty() && l[0] != '#' && l[0] != ' ' && l[0] != '\t')
			{
				sectionHeaderIdx = i;
				inAdditionalApps = true;
			}
		}
		else
		{
			if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
				break;

			size_t dashPos = l.find("- ");
			if (dashPos != std::string::npos)
			{
				bool leadingWS = true;
				for (size_t j = 0; j < dashPos; ++j)
				{
					if (l[j] != ' ' && l[j] != '\t') { leadingWS = false; break; }
				}
				if (leadingWS) lastEntryIdx = i;
			}
		}
	}

	if (sectionHeaderIdx == -1)
	{
		g_pLog->info("addAdditionalAppId: AdditionalApps section not found in %s\n", configPath.c_str());
		return false;
	}

	// 4. Build the new entry and insert it
	std::string newEntry = "  - " + std::to_string(appId);
	int insertAfter = (lastEntryIdx != -1) ? lastEntryIdx : sectionHeaderIdx;
	lines.insert(lines.begin() + insertAfter + 1, newEntry);

	// 5. Write atomically: write to temp file, then rename
	std::string tmpPath = configPath + ".tmp";
	std::ofstream outFile(tmpPath);
	if (!outFile.is_open())
	{
		g_pLog->info("addAdditionalAppId: Cannot write temp file at %s\n", tmpPath.c_str());
		return false;
	}

	for (size_t i = 0; i < lines.size(); ++i)
	{
		outFile << lines[i];
		if (i + 1 < lines.size())
			outFile << '\n';
	}
	outFile.close();

	if (std::rename(tmpPath.c_str(), configPath.c_str()) != 0)
	{
		g_pLog->info("addAdditionalAppId: Failed to rename temp file to %s\n", configPath.c_str());
		std::remove(tmpPath.c_str());
		return false;
	}

	g_pLog->info("addAdditionalAppId: Appended AppID %u to AdditionalApps in %s\n", appId, configPath.c_str());
	// CFileWatcher will detect the change and trigger loadSettings() automatically
	return true;
}

void scanLuaPluginsAndUpdateConfig()
{
	// 1. Resolve $SteamRoot — try known paths, use whichever exists
	std::string pluginDir = g_config.getPluginDir();

	if (pluginDir.empty())
	{
		g_pLog->warn("scanLuaPluginsAndUpdateConfig: Cannot locate SteamRoot or plugin directory\n");
		return;
	}

	// Regex to extract AppID from first addappid(<digits>) call
	const std::regex appIdRegex(R"(addappid\((\d+))");

	// 3. Collect AppIDs from all *.lua files
	std::unordered_set<uint32_t> collectedAppIds;

	for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
	{
		if (!entry.is_regular_file())
			continue;

		const auto& path = entry.path();
		if (path.extension() == ".lua")
		{
			// Read file content
			std::ifstream file(path);
			if (!file.is_open())
			{
				g_pLog->warn("scanLuaPluginsAndUpdateConfig: Cannot open Lua plugin: %s\n", path.c_str());
				continue;
			}

			std::string content((std::istreambuf_iterator<char>(file)),
								 std::istreambuf_iterator<char>());
			file.close();

			// 4. Extract AppID from first addappid(\d+) match
			std::smatch match;
			if (!std::regex_search(content, match, appIdRegex))
			{
				g_pLog->warn("Malformed Lua plugin: %s\n", path.c_str());
				continue;
			}

			try
			{
				const uint32_t appId = static_cast<uint32_t>(std::stoul(match[1].str()));
				collectedAppIds.emplace(appId);
				g_pLog->debug("scanLuaPluginsAndUpdateConfig: Found AppID %u in %s\n", appId, path.c_str());
			}
			catch (...)
			{
				g_pLog->warn("Malformed Lua plugin: %s\n", path.c_str());
			}
		}
		else if (path.extension() == ".manifest")
		{
			// Extract AppID from filename (e.g. 293781_9207527406397102173.manifest)
			std::string stem = path.stem().string();
			size_t underscorePos = stem.find('_');
			std::string appIdStr = (underscorePos != std::string::npos) ? stem.substr(0, underscorePos) : stem;
			
			try
			{
				const uint32_t appId = static_cast<uint32_t>(std::stoul(appIdStr));
				collectedAppIds.emplace(appId);
				g_pLog->debug("scanLuaPluginsAndUpdateConfig: Found AppID %u from manifest %s\n", appId, path.c_str());
			}
			catch (...)
			{
				// Not a valid AppID filename, ignore
			}
		}
	}

	// 5. For each collected AppID: if not already in addedAppIds, add it
	for (const uint32_t appId : collectedAppIds)
	{
		if (!g_config.isAddedAppId(appId))
		{
			g_pLog->info("scanLuaPluginsAndUpdateConfig: Adding AppID %u to AdditionalApps\n", appId);
			g_config.addAdditionalAppId(appId);
		}
		else
		{
			g_pLog->debug("scanLuaPluginsAndUpdateConfig: AppID %u already in AdditionalApps, skipping\n", appId);
		}
	}

	g_pLog->info("scanLuaPluginsAndUpdateConfig: Scan complete, processed %zu Lua plugin(s)\n", collectedAppIds.size());
}

CConfig g_config = CConfig();
