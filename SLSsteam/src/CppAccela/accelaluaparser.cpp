#include "accelaluaparser.hpp"

// Safe for use in forked child — no globals, no mutexes
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#define LUAPARSER_LOG(fmt, ...) \
    fprintf(stderr, "[LuaParser] " fmt __VA_OPT__(,) __VA_ARGS__)

namespace CppAccela::LuaParser
{
    // ── Internal helpers ──────────────────────────────────────────────────────

    /** Trim leading and trailing whitespace from a string (in-place). */
    static void trim(std::string& s)
    {
        const auto notSpace = [](unsigned char c){ return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    }

    static std::string trimmed(std::string s)
    {
        trim(s);
        return s;
    }

    /**
     * Remove surrounding double-quotes from a string if present.
     * e.g.  "aabbccdd"  →  aabbccdd
     */
    static std::string stripQuotes(const std::string& s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    /**
     * Extract the trailing Lua comment from a line.
     * e.g.  "addappid(730, 1, 0)  -- Counter-Strike 2"  →  "Counter-Strike 2"
     * Returns empty string if no comment present.
     */
    static std::string extractComment(const std::string& rest)
    {
        const auto pos = rest.find("--");
        if (pos == std::string::npos) return {};
        return trimmed(rest.substr(pos + 2));
    }

    // ── addappid() parsing ────────────────────────────────────────────────────

    /**
     * Parse all addappid(...) calls from `luaContent`.
     *
     * Lua plugin format for addappid:
     *   addappid( <appId> )                          — DLC (no key)
     *   addappid( <appId> , 1 , 0 )                  — main app (no key, flag=0)
     *   addappid( <appId> , 1 , "<hexKey>" )          — depot with key
     *
     * The parser mirrors Python's:
     *   re.finditer(r"addappid\((.*?)\)(.*)", content, re.IGNORECASE)
     *
     * First match → appId + gameName from comment.
     * Subsequent matches with 3rd arg being a non-empty quoted string → depot.
     * Subsequent matches with ≤2 args or 3rd arg == "0" → DLC.
     */
    static void parseAddAppIds(const std::string& luaContent, ParseResult& result)
    {
        // Case-insensitive match of  addappid( <args> ) <rest-of-line>
        // Group 1: everything inside the parens
        // Group 2: everything after the closing paren on that line
        const std::regex re(
            R"delim(addappid\s*\((.*?)\)(.*))delim",
            std::regex::icase
        );

        auto begin = std::sregex_iterator(luaContent.begin(), luaContent.end(), re);
        const auto end  = std::sregex_iterator{};

        bool firstMatch = true;
        for (auto it = begin; it != end; ++it)
        {
            const std::smatch& m = *it;
            const std::string argsStr = trimmed(m[1].str());
            const std::string rest    = m[2].str();

            // Split args by comma
            std::vector<std::string> args;
            {
                std::istringstream ss(argsStr);
                std::string token;
                while (std::getline(ss, token, ','))
                    args.push_back(trimmed(token));
            }
            if (args.empty()) continue;

            const std::string id = args[0];
            if (id.empty()) continue;

            const std::string comment = extractComment(rest);

            if (firstMatch)
            {
                // First addappid() → this is the main app
                if (result.appId.empty())
                {
                    result.appId = id;
                    // Only use inline comment as name if header didn't set it
                    if (result.gameName.empty())
                        result.gameName = comment;
                }
                firstMatch = false;
                continue;
            }

            // Skip entries that match the main appId (some plugins repeat it)
            if (id == result.appId)
            {
                // Only use inline comment as name if nothing better found yet
                if (result.gameName.empty() && !comment.empty())
                    result.gameName = comment;
                continue;
            }

            // Determine if this is a depot (has a non-empty, non-zero key)
            // 3rd argument index = 2
            bool isDepot = false;
            std::string depotKey;
            if (args.size() >= 3)
            {
                std::string rawKey = trimmed(args[2]);
                rawKey = stripQuotes(rawKey);
                if (!rawKey.empty() && rawKey != "0")
                {
                    isDepot  = true;
                    depotKey = rawKey;
                }
            }

            if (isDepot)
            {
                DepotInfo info;
                info.depotId     = id;
                info.key         = depotKey;
                info.description = comment.empty() ? ("Depot " + id) : comment;
                result.depots[id] = std::move(info);
            }
            else
            {
                // No key → DLC
                result.dlcs[id] = comment.empty() ? ("DLC " + id) : comment;
            }
        }
    }

    // ── setManifestid() parsing ───────────────────────────────────────────────

    /**
     * Parse all setManifestid(...) calls.
     *
     * Formats handled:
     *   setManifestid( 1234561 , "9876543210987654321" )
     *   setManifestid( 1234561 , "9876543210987654321" , 4096000000 )
     *
     * Captures: [1] depotId, [2] manifestGid, optional [3] sizeBytes
     */
    static void parseManifestIds(const std::string& luaContent, ParseResult& result)
    {
        const std::regex re(
            R"delim([Ss]et[Mm]anifest[Ii][Dd]\s*\(\s*(\d+)\s*,\s*"(\d+)"\s*(?:,\s*(\d+)\s*)?\))delim",
            std::regex::ECMAScript
        );

        auto begin = std::sregex_iterator(luaContent.begin(), luaContent.end(), re);
        const auto end  = std::sregex_iterator{};

        for (auto it = begin; it != end; ++it)
        {
            const std::smatch& m = *it;
            const std::string depotId    = m[1].str();
            const std::string manifestGid = m[2].str();
            const std::string sizeBytes  = m[3].matched ? m[3].str() : "";

            result.manifests[depotId]     = manifestGid;
            result.manifestSizes[depotId] = sizeBytes;

            // Also store directly on the DepotInfo if it exists
            auto depotIt = result.depots.find(depotId);
            if (depotIt != result.depots.end())
            {
                depotIt->second.manifestGid = manifestGid;
                depotIt->second.sizeBytes   = sizeBytes;
            }
        }
    }

    // ── addtoken() parsing ────────────────────────────────────────────────────

    /**
     * Extract the app access token from:
     *   addtoken( <appId> , "<token>" )
     *
     * Mirrors Python's:
     *   r'addtoken\s*\(\s*\d+\s*,\s*"([^"]+)"\s*\)'
     */
    static void parseAppToken(const std::string& luaContent, ParseResult& result)
    {
        const std::regex re(
            R"delim(addtoken\s*\(\s*\d+\s*,\s*"([^"]+)"\s*\))delim",
            std::regex::icase
        );

        std::smatch m;
        if (std::regex_search(luaContent, m, re))
        {
            result.appToken = m[1].str();
            LUAPARSER_LOG("found app token for appid=%s (%.10s...)\n",
                     result.appId.c_str(), result.appToken.c_str());
        }
    }

    // ── Public API ────────────────────────────────────────────────────────────

    ParseResult parse(const std::string& luaContent)
    {
        ParseResult result;

        if (luaContent.empty())
        {
            LUAPARSER_LOG("empty content\n");
            return result;
        }

        // 0. Extract --Gamename <name> header comment (highest priority for name)
        //    Format:  --Gamename Crawl      or   -- Gamename Crawl
        {
            const std::regex reHeader(
                R"delim(^--\s*[Gg]amename\s+(.+)$)delim",
                std::regex::multiline
            );
            std::smatch mh;
            if (std::regex_search(luaContent, mh, reHeader))
            {
                result.gameName = trimmed(mh[1].str());
                LUAPARSER_LOG("found --Gamename header: '%s'\n",
                              result.gameName.c_str());
            }
        }

        // 1. Parse all addappid() calls (fills appId, depots, dlcs;
        //    gameName only written if not already set from header)
        parseAddAppIds(luaContent, result);

        if (result.appId.empty())
        {
            LUAPARSER_LOG("no addappid() found — invalid Lua plugin\n");
            return result;
        }

        // 2. Parse setManifestid() calls (fills manifests, manifestSizes)
        parseManifestIds(luaContent, result);

        // 3. Validate that at least one manifest exists
        //    A Lua without manifests cannot be used for downloads
        if (result.manifests.empty())
        {
            LUAPARSER_LOG("no valid setManifestid() found — Lua cannot be used for downloads\n");
            result.valid = false;
            return result;
        }

        // 4. Parse addtoken() (fills appToken)
        parseAppToken(luaContent, result);

        // Fallback game name
        if (result.gameName.empty())
            result.gameName = "App_" + result.appId;

        result.valid = true;

        LUAPARSER_LOG("appid=%s name='%s' depots=%zu dlcs=%zu manifests=%zu token=%s\n",
                 result.appId.c_str(),
                 result.gameName.c_str(),
                 result.depots.size(),
                 result.dlcs.size(),
                 result.manifests.size(),
                 result.appToken.empty() ? "none" : "present");

        return result;
    }

    ParseResult parseFile(const std::string& luaPath)
    {
        std::ifstream f(luaPath);
        if (!f.is_open())
        {
            LUAPARSER_LOG("cannot open %s\n", luaPath.c_str());
            return {};
        }

        std::ostringstream ss;
        ss << f.rdbuf();
        return parse(ss.str());
    }

} // namespace CppAccela::LuaParser
