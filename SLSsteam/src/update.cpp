#include "update.hpp"

#include "config.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "utils.hpp"
#include "version.hpp"

#include <curl/curl.h>
#include <curl/easy.h>
#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <map>
#include <string>

static CURL* curl = nullptr;

std::map<uint64_t, std::unordered_set<std::string>> Updater::clientHashMap = std::map<uint64_t, std::unordered_set<std::string>>();

static size_t writeCallback(const char* content, size_t size, size_t memberSize, std::string* data)
{
	data->append(content, size * memberSize);
	return size * memberSize;
}

bool Updater::init()
{
	std::string data;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "https://raw.githubusercontent.com/AceSLS/SLSsteam/refs/heads/main/res/updates.yaml");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

	auto res = curl_easy_perform(curl);
	g_pLog->info("Curl Res: %u\n", res);

	curl_easy_cleanup(curl);

	if(res != 0)
	{
		data = loadFromCache();
		if(data.size() < 1)
		{
			return false;
		}

		g_pLog->info("Using cached updates.yaml\n");
	}

	g_pLog->debug("updates.yaml:\n%s\n", data.c_str());

	try
	{
		YAML::Node node = YAML::Load(data);
		for (const auto& sub : node["SafeModeHashes"])
		{
			uint64_t version = sub.first.as<uint64_t>();
			clientHashMap[version] = std::unordered_set<std::string>();

			g_pLog->debug("Parsing version %llu\n", version);

			for(const auto& hash : sub.second)
			{
				auto str = hash.as<std::string>();
				clientHashMap[version].emplace(str);

				g_pLog->debug("Added %s to SLSsteam version %llu\n", str.c_str(), version);
			}
		}
	}
	catch(...)
	{
		g_pLog->info("Failed to parse updates!\n");
		return false;
	}

	saveToCache(data);
	return true;
}

std::string Updater::getCacheFilePath()
{
	auto path = g_config.getDir().append("/.updates.yaml");
	return path;
}

void Updater::saveToCache(std::string yaml)
{
	auto path = Updater::getCacheFilePath();

	std::ofstream stream = std::ofstream(path.c_str());
	stream << yaml;
	stream.close();

	g_pLog->debug("Cached res/updates.yaml!\n");
}

std::string Updater::loadFromCache()
{
	auto path = Updater::getCacheFilePath();
	if (!std::filesystem::exists(path))
	{
		return std::string();
	}

	g_pLog->debug("Loading updates.ymal from disk!\n");

	std::ifstream fstream = std::ifstream(path.c_str());
	std::stringstream buf;
	buf << fstream.rdbuf();

	fstream.close();
	return buf.str();
}

bool Updater::verifySafeModeHash()
{
	auto path = std::filesystem::path(g_modSteamClient.path);

	try
	{
		std::string sha256 = Utils::getFileSHA256(path.c_str());
		g_pLog->info("steamclient.so hash is %s\n", sha256.c_str());

		if (!clientHashMap.contains(VERSION))
		{
			return false;
		}

		const auto& safeHashes = clientHashMap[VERSION];
		if (safeHashes.contains(sha256))
		{
			return true;
		}

		return false;
	}
	catch(std::runtime_error& err)
	{
		g_pLog->debug("Unable to read steamclient.so hash!\n");
		return false;
	}

	return true;
}
