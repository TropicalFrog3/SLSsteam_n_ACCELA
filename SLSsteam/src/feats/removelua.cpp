#include "removelua.hpp"
#include "cdpinject.hpp"
#include "../log.hpp"
#include "../config.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace RemoveLua
{
    std::string loadResourceFile(const std::string& filename)
    {
        std::vector<std::string> paths;

        // 1. Try to resolve relative to SLSsteam.so location using dladdr
        Dl_info info;
        if (dladdr((void*)loadResourceFile, &info) && info.dli_fname)
        {
            try
            {
                std::filesystem::path libPath(info.dli_fname);
                std::filesystem::path libDir = libPath.parent_path();
                paths.push_back((libDir / filename).string());
                paths.push_back((libDir / "res" / "inject-scripts" / filename).string());
                paths.push_back((libDir / ".." / "res" / "inject-scripts" / filename).string());
            }
            catch (...) {}
        }

        // 2. Add current working directory candidates
        paths.push_back(filename);
        paths.push_back("./res/inject-scripts/" + filename);
        paths.push_back("../res/inject-scripts/" + filename);

        // 3. Add home folder ~/.local/share/SLSsteam candidates
        const char* home = getenv("HOME");
        if (home)
        {
            paths.push_back(std::string(home) + "/.local/share/SLSsteam/" + filename);
            paths.push_back(std::string(home) + "/.local/share/SLSsteam/res/inject-scripts/" + filename);
        }

        for (const auto& path : paths) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                LOG_INFO("CDPInject::loadResourceFile: Successfully loaded %s from %s\n", filename.c_str(), path.c_str());
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }

        LOG_INFO("CDPInject::loadResourceFile: Could not find %s in any candidate path!\n", filename.c_str());
        return "";
    }

    bool injectRemoveLuaScript()
    {
        auto pages = CDPInject::fetchPages();
        if (pages.empty()) return false;

        std::vector<std::string> pagesNeedAppDetails;
        std::vector<std::string> pagesNeedImportLua;
        std::vector<std::string> pagesNeedAutoCollection;

        for (auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty()) continue;

            if (page.title == "SharedJSContext") {
                if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsAutoCollectionInjected"))
                {
                    pagesNeedAutoCollection.push_back(page.webSocketDebuggerUrl);
                }
                continue;
            }

            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsAppDetailsInjected"))
            {
                pagesNeedAppDetails.push_back(page.webSocketDebuggerUrl);
            }
            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsImportLuaInjected"))
            {
                pagesNeedImportLua.push_back(page.webSocketDebuggerUrl);
            }
        }

        if (pagesNeedAppDetails.empty() && pagesNeedImportLua.empty() && pagesNeedAutoCollection.empty())
        {
            return true;
        }

        // Lazy-loaded cache
        static std::string cachedAppDetailsScript;
        static std::string cachedImportLuaScript;
        static std::string cachedAutoCollectionScript;

        if (!pagesNeedAppDetails.empty() && cachedAppDetailsScript.empty())
        {
            cachedAppDetailsScript = loadResourceFile("app-details-script.js");
            if (cachedAppDetailsScript.empty()) {
                LOG_DEBUG("Failed to load app-details-script.js");
                return false;
            }
        }

        if (!pagesNeedImportLua.empty() && cachedImportLuaScript.empty())
        {
            cachedImportLuaScript = loadResourceFile("import-lua-script.js");
            if (cachedImportLuaScript.empty()) {
                LOG_DEBUG("Failed to load import-lua-script.js");
                return false;
            }
        }
        
        if (!pagesNeedAutoCollection.empty() && cachedAutoCollectionScript.empty())
        {
            cachedAutoCollectionScript = loadResourceFile("auto-collection.js");
            if (!cachedAutoCollectionScript.empty()) {
                cachedAutoCollectionScript = "window.__slsAutoCollectionInjected = true;\n" + cachedAutoCollectionScript;
            }
        }

        for (const auto& wsUrl : pagesNeedAppDetails)
        {
            CDPInject::injectJS(wsUrl, cachedAppDetailsScript);
        }

        for (const auto& wsUrl : pagesNeedImportLua)
        {
            CDPInject::injectJS(wsUrl, cachedImportLuaScript);
        }

        for (const auto& wsUrl : pagesNeedAutoCollection)
        {
            CDPInject::injectJS(wsUrl, cachedAutoCollectionScript);
        }

        return true;
    }
}
