#include "sdk/sdk.hpp"

#include "api.hpp"
#include "config.hpp"
#include "decompiler.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "log.hpp"
#include "patterns.hpp"
#include "update.hpp"
#include "utils.hpp"
#include "feats/cefsizefix.hpp"
#include "feats/depotkeys.hpp"
#include "feats/storeinject.hpp"
#include "feats/apps.hpp"
#include "feats/removelua.hpp"
#include "feats/tier0hook.hpp"
#include "feats/autoupdate.hpp"
#include "vftableinfo.hpp"

#include "libmem/libmem.h"
#include <curl/curl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <link.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>


static bool cleanEnvVar(const char* varName, const char* endsWith)
{
	char* var = getenv(varName);
	if (var == NULL)
		return false;

	const auto splits = Utils::strsplit(var, ":");
	auto newEnv = std::string();

	for (unsigned int i = 0; i < splits.size(); i++)
	{
		const auto split = splits.at(i);
		if (split.ends_with(endsWith))
		{
			LOG_DEBUG("Removed %s from $%s\n", endsWith, varName);
			continue;
		}

		if (newEnv.size() > 0)
		{
			newEnv.append(":");
		}
		newEnv.append(split);
	}

	if (newEnv.size())
	{
		setenv(varName, newEnv.c_str(), true);
	}
	else
	{
		unsetenv(varName);
	}
	//LOG_DEBUG("Set %s to %s\n", varName, newEnv.c_str());

	return true;
}

//Looking at /proc/self/maps it seems like this isn't needed for processes that aren't steam
//__attribute__((noreturn))
static void unload()
{
	Tier0Hook::remove();
	CefSizeFix::removeSizeFixScript();
	StoreInject::shutdown();
	Hooks::remove();
	curl_global_cleanup();
	//Hooks::remove();

	//This is absolutely unnessecary for applications loading SLSsteam where it cancels from setup()
	//Would be nice to run have for failed load() attempts though 
	//lm_module_t mod;
	//if (LM_FindModule("SLSsteam.so", &mod))
	//{
	//	//TODO: Investigate crash ?
	//	//Possibly: Might be because we're unmapping what ever thread we're running in
	//	//munmap(reinterpret_cast<void*>(mod.base), mod.size);
	//}
	//exit(0);
}

//TODO: Remove when unload() works properly since it should not be needed anymore after that
static bool setupSuccess = false;

static void runPendingReleaseInstaller()
{
	const char* home = getenv("HOME");
	if (!home)
	{
		return;
	}

	const std::string installDirs[] = {
		std::string(home) + "/.local/share/SLSsteam",
		std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam"
	};

	for (const auto& installDir : installDirs)
	{
		const auto marker = std::filesystem::path(installDir) / ".pending-full-update";
		// update-all.sh is a normal installed resource, not evidence of a
		// pending update. Only the marker created by the accepted update flow
		// may trigger the detached full installer.
		if (!std::filesystem::exists(marker))
		{
			continue;
		}

		std::string extractDir;
		if (std::filesystem::exists(marker))
		{
			std::ifstream markerFile(marker);
			std::getline(markerFile, extractDir);
			markerFile.close();
			std::filesystem::remove(marker);
		}

		if (extractDir.empty())
		{
			LOG_WARN("AutoUpdate: Pending update marker is empty\n");
			continue;
		}

		const std::filesystem::path installer = std::filesystem::path(extractDir) / "install.sh";

		if (!std::filesystem::exists(installer))
		{
			continue;
		}

		const pid_t pid = fork();
		if (pid < 0)
		{
			LOG_WARN("AutoUpdate: Failed to start pending full installer\n");
			continue;
		}

		if (pid == 0)
		{
			setsid();
			const std::string cleanupCommand =
				"bash \"$1/install.sh\"; status=$?; rm -rf -- \"$1\"; exit $status";
			const char* commandArg = extractDir.c_str();
			execlp("bash", "bash", "-c", cleanupCommand.c_str(), "slssteam-full-update", commandArg, nullptr);
			_exit(127);
		}

		LOG_INFO("AutoUpdate: Starting pending ACCELA and Headcrab update\n");
		return;
	}
}

static void setup()
{
	lm_process_t proc {};
	if (!LM_GetProcess(&proc))
	{
		unload();
		return;
	}

	//Do not do anything in other processes
	if (strcmp(proc.name, "steam") != 0)
	{
		unload();
		return;
	}

	g_pLog = std::unique_ptr<CLog>(CLog::createDefaultLog());
	curl_global_init(CURL_GLOBAL_ALL);
	//Won't happen, log throws a runtime exception when creation fails.
	//But just in case I decide to refactor one day
	if (!g_pLog)
	{
		unload();
		return;
	}

	runPendingReleaseInstaller();

	LOG_DEBUG("SLSsteam loading in %s\n", proc.name);

	//Any release
	cleanEnvVar("LD_AUDIT", "SLSsteam.so");
	cleanEnvVar("LD_AUDIT", "library-inject.so");

	//Arch release
	cleanEnvVar("LD_AUDIT", "libSLSsteam.so");
	cleanEnvVar("LD_AUDIT", "libSLS-library-inject.so");
	//TODO: Investigate weird logging. Not like it's necessary anymore
	//cleanEnvVar("LD_PRELOAD");

	if (!g_config.init())
	{
		unload();
		return;
	}

	scanLuaPluginsAndUpdateConfig();
	DepotKeys::scanLuaPluginsForDepotKeys();

	//Since we can't statically link everything and some distros seem to respect LD_LIBRARY_PATH
	//more or less than mine does we just force append those
	//Hopefully this won't mess anything else up
	const char* env_ldLibPath = getenv("LD_LIBRARY_PATH");
	auto ldLibPath = std::string(env_ldLibPath ? env_ldLibPath : "");
	if (!ldLibPath.empty()) {
		ldLibPath.append(":");
	}
	ldLibPath.append("/usr/lib:/usr/lib32");
	setenv("LD_LIBRARY_PATH", ldLibPath.c_str(), true);

	Updater::init();
	AutoUpdate::checkAndPrompt();

	setupSuccess = true;
}

static void load()
{
	if (!setupSuccess)
	{
		return;
	}

	if (!g_modSteamClient.base || !g_modSteamUI.base || !g_modTier0.base)
	{
		return;
	}

	const auto path = std::filesystem::path(g_modSteamClient.path);
	const auto dir = path.parent_path();

	LOG_INFO
	(
		"steamclient.so loaded from %s/%s at 0x%x to 0x%x\n",
		dir.filename().c_str(),
		path.filename().c_str(),
		g_modSteamClient.base,
		g_modSteamClient.end
	);
	LOG_INFO
	(
		"steamui.so loaded at 0x%x to 0x%x\n",
		g_modSteamUI.base,
		g_modSteamUI.end
	);

	if (!Updater::verifySafeModeHash())
	{
		if (g_config.safeMode.get())
		{
			LOG_NOTIFYERROR("Unknown steamclient.so hash! Aborting...");
			unload();
			return;
		}
		else if (g_config.warnHashMissmatch.get())
		{
			LOG_NOTIFYWARN("steamclient.so hash missmatch! Please update :)");
		}
	}

	if (!Steam::init())
	{
		LOG_NOTIFYERROR("Failed to find steam exports!\n");
		return;
	}

	if (!VFTIndexes::init())
	{
		LOG_NOTIFYERROR("Failed to parse VFTables! Aborting...");
		return;
	}

	if (!Patterns::init())
	{
		LOG_NOTIFYERROR("Failed to find all patterns! Aborting...");
		return;
	}

	if (!Hooks::setup())
	{
		unload();
		return;
	}

	SLSAPI::init();

	// Install the tier0 hook to intercept steamwebhelper launch
	// This replaces --cef-enable-debugging with --remote-debugging-pipe
	// Must be done early so it catches the next steamwebhelper spawn
	if (Tier0Hook::install()) {
		LOG_INFO("Tier0 hook installed - steamwebhelper will use pipe-based CDP\n");
	} else {
		LOG_WARN("Tier0 hook failed - falling back to port-based CDP\n");
	}

	// Inject the CEF size fix script into Steam's UI
	CefSizeFix::injectSizeFixScript();

	// Start the Store injection background thread (callback server on port 9001)
	// MUST start before RemoveLua injection, because the JS depends on the /check API
	StoreInject::init();

	// Inject the Remove Lua button script (needs port 9001 to be listening)
	RemoveLua::injectRemoveLuaScript();

	Decompiler::cleanUp();

	if (g_config.notifyInit.get())
	{
		const auto now = std::chrono::time_point { std::chrono::system_clock::now() };
		const auto ymd = std::chrono::year_month_day { std::chrono::floor<std::chrono::days>(now) };

		//Funsy easter egg :)
		if (static_cast<unsigned int>(ymd.month()) == 2 && static_cast<unsigned int>(ymd.day()) == 22)
		{
			LOG_NOTIFY("Happy birthday SLSsteam!");
		}
		else
		{
			LOG_NOTIFY("Loaded successfully");
		}
	}
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}

unsigned int la_objopen(struct link_map *map, __attribute__((unused)) Lmid_t lmid, __attribute__((unused)) uintptr_t *cookie)
{
	// The loader may provide an unnamed link map during early startup.
	if (!map || !map->l_name)
	{
		return 0;
	}

	const std::string name(map->l_name);

	if (name.ends_with("/steamclient.so"))
	{
		//Analyse modules before any relocations get applied
		LM_FindModule("steamclient.so", &g_modSteamClient);
		Decompiler::parseModule(g_modSteamClient);
		//This is wasteful, but we have to analyse right away otherwise the offset get turned into
		//addresses messing up the analysis.
		//We could workaround it by only loading after a late module has been loaded
		for (auto& vft : Decompiler::vftables)
		{
			vft.second.analyze();
		}

		load();
	}
	if (name.ends_with("/steamui.so"))
	{
		//Analyse modules before any relocations get applied
		LM_FindModule("steamui.so", &g_modSteamUI);
		Decompiler::parseModule(g_modSteamUI);
		//This is wasteful, but we have to analyse right away otherwise the offset get turned into
		//addresses messing up the analysis.
		//We could workaround it by only loading after a late module has been loaded
		for (auto& vft : Decompiler::vftables)
		{
			vft.second.analyze();
		}

		load();
	}
	if (name.ends_with("/libtier0_s.so"))
	{
		LM_FindModule("libtier0_s.so", &g_modTier0);

		load();
	}
	// Retry tier0 hook when libtier0_s.so is loaded (it may load after steamclient.so)
	if (setupSuccess && name.ends_with("/libtier0_s.so"))
	{
		if (!Tier0Hook::isHookInstalled() && Tier0Hook::install())
		{
			LOG_INFO("Tier0 hook installed via la_objopen (libtier0_s.so just loaded)\n");
		}
	}

	return 0;
}

void la_preinit(__attribute__((unused)) uintptr_t *cookie)
{
	setup();
}
