#include "accelamanifest.hpp"

#include "../curl.hpp"
#include "../atomic_file.hpp"

// Safe for forked child — no LOG_* globals
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#define MANIFEST_LOG(fmt, ...) \
    fprintf(stderr, "[AccelaManifest] " fmt __VA_OPT__(,) __VA_ARGS__)

namespace fs = std::filesystem;

namespace CppAccela::Manifest
{
    // ── Lua parsing ───────────────────────────────────────────────────────────

    bool validateLua(const std::string& luaContent)
    {
        // Case-insensitive check for at least one addappid( call
        const std::string lower = [&]() {
            std::string s = luaContent;
            for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            return s;
        }();
        return lower.find("addappid(") != std::string::npos;
    }

    std::vector<DepotManifest> parseManifestPairs(const std::string& luaContent)
    {
        std::vector<DepotManifest> result;

        // Matches:
        //   setManifestid( 293781 , "9207527406397102173" )
        //   setManifestid( 293781 , "9207527406397102173" , 1234567 )
        // Case-insensitive via regex flag.
        // Capture groups: [1] depotId, [2] manifestGid
        const std::regex re(
            R"delim([Ss]et[Mm]anifest[Ii][Dd]\s*\(\s*(\d+)\s*,\s*"(\d+)")delim",
            std::regex::ECMAScript
        );

        auto begin = std::sregex_iterator(luaContent.begin(), luaContent.end(), re);
        const auto end = std::sregex_iterator{};

        for (auto it = begin; it != end; ++it)
        {
            const std::smatch& m = *it;
            result.push_back({ m[1].str(), m[2].str() });
        }

        return result;
    }

    // ── manifest.steam.run API fallback ───────────────────────────────────────

    /**
     * Ask https://manifest.steam.run/api/depot/<appId> for manifest IDs.
     *
     * The response is a JSON array like:
     *   [{"depotid":293781,"manifestid":"9207527406397102173",...}, ...]
     *
     * We do simple string-based parsing — no JSON library needed.
     * Returns a map of depotId → manifestGid extracted from the response.
     */
    static std::vector<DepotManifest> fetchMissingFromApi(
        const std::string& appId,
        const std::vector<std::string>& missingDepots)
    {
        std::vector<DepotManifest> found;
        if (missingDepots.empty()) return found;

        const std::string url =
            "https://manifest.steam.run/api/depot/" + appId;

        MANIFEST_LOG("querying manifest API for %zu missing depot(s): %s\n",
                 missingDepots.size(), url.c_str());

        std::string body;
        const int rc = Curl::getString(url.c_str(), body);

        if (rc != 0 || body.empty())
        {
            MANIFEST_LOG("manifest API request failed (curl exit %d)\n", rc);
            return found;
        }

        // Quick sanity check — the response must look like our JSON array
        if (body.find("\"depots\"") == std::string::npos &&
            body.find("depotid") == std::string::npos)
        {
            MANIFEST_LOG("API response does not look like depot JSON (%.120s)\n",
                     body.c_str());
            return found;
        }

        for (const auto& depotId : missingDepots)
        {
            const std::regex reDepot(
                R"delim("depotid"\s*:\s*)" + depotId + R"([^}]*"manifestid"\s*:\s*"(\d+)")delim",
                std::regex::ECMAScript
            );

            std::smatch m;
            if (std::regex_search(body, m, reDepot))
            {
                MANIFEST_LOG("API resolved depot %s -> manifest GID %s\n",
                         depotId.c_str(), m[1].str().c_str());
                found.push_back({ depotId, m[1].str() });
            }
            else
            {
                MANIFEST_LOG("API did not return manifest info for depot %s\n",
                         depotId.c_str());
            }
        }

        return found;
    }

    // ── Main collection ───────────────────────────────────────────────────────

    CollectResult collectManifestFiles(const std::string& appId,
                                       const std::vector<DepotManifest>& pairs,
                                       const std::string& depotcacheDirPath,
                                       const std::string& placeholderDir)
    {
        CollectResult result;
        result.ok = false;

        if (pairs.empty())
        {
            MANIFEST_LOG("no manifest pairs to resolve\n");
            return result;
        }

        std::vector<std::string> missingDepots;

        for (const auto& dm : pairs)
        {
            const std::string filename = dm.depotId + "_" + dm.manifestGid + ".manifest";
            const fs::path    fullPath = fs::path(depotcacheDirPath) / filename;

            if (fs::exists(fullPath))
            {
                MANIFEST_LOG("found local manifest %s\n", filename.c_str());
                result.manifestFiles.push_back(fullPath.string());
            }
            else
            {
                MANIFEST_LOG("manifest not found locally: %s\n", filename.c_str());
                missingDepots.push_back(dm.depotId);
            }
        }

        if (!missingDepots.empty())
        {
            const auto apiResults = fetchMissingFromApi(appId, missingDepots);

            if (!apiResults.empty())
            {
                fs::create_directories(placeholderDir);

                for (const auto& dm : apiResults)
                {
                    const std::string filename =
                        dm.depotId + "_" + dm.manifestGid + ".manifest";
                    const std::string placeholderPath =
                        placeholderDir + "/" + filename;

                    if (AtomicFile::write(placeholderPath, "", true))
                    {
                        MANIFEST_LOG("created placeholder %s\n", filename.c_str());
                        result.manifestFiles.push_back(placeholderPath);
                    }
                    else
                    {
                        MANIFEST_LOG("could not create placeholder at %s\n",
                                 placeholderPath.c_str());
                    }
                }
            }
            else
            {
                MANIFEST_LOG("API returned no results for %zu depot(s)\n",
                         missingDepots.size());
            }
        }

        result.ok = !result.manifestFiles.empty();

        // Even if no manifest files were resolved locally or via API,
        // we can still proceed — DepotDownloaderMod will fetch manifests
        // from Steam's CDN using the manifest GIDs in the Lua plugin.
        // Mark ok=true as long as we have valid manifest pairs to attempt.
        if (!result.ok && !pairs.empty())
        {
            MANIFEST_LOG("no manifest files resolved, but will attempt CDN download for %zu depot(s)\n",
                     pairs.size());
            result.ok = true;
        }

        MANIFEST_LOG("resolved %zu manifest file(s) (local + API) for appid=%s\n",
                 result.manifestFiles.size(), appId.c_str());

        return result;
    }

} // namespace CppAccela::Manifest
