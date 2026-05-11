#include "tier0hook.hpp"
#include "../log.hpp"
#include "../memhlp.hpp"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "libmem/libmem.h"

namespace Tier0Hook
{
    // ── Hook state ──
    // We use libmem's LM_HookCode for the detour, same pattern as the rest of SLSsteam
    static lm_address_t g_createSimpleProcessAddr = LM_ADDRESS_BAD;
    static lm_address_t g_trampoline = LM_ADDRESS_BAD;
    static lm_size_t g_hookSize = 0;
    static bool g_hookInstalled = false;

    // ── Command line parsing helper ──

    /**
     * Parse a command string into executable + args.
     * steamwebhelper commands are space-separated.
     */
    struct ParsedCmd {
        std::string executable;
        std::vector<std::string> args;

        std::string build() const {
            std::ostringstream oss;
            oss << executable;
            for (auto& a : args) {
                oss << " " << a;
            }
            return oss.str();
        }

        void ensureParam(const std::string& flag) {
            for (auto& a : args) {
                if (a == flag) return;
            }
            args.push_back(flag);
        }

        void ensureParamWithValue(const std::string& flag, const std::string& value) {
            std::string combined = flag + "=" + value;
            for (auto& a : args) {
                if (a.find(flag + "=") == 0) {
                    a = combined; // Update existing value
                    return;
                }
            }
            args.push_back(combined);
        }
    };

    static ParsedCmd parseCommand(const char* cmd) {
        ParsedCmd result;
        std::string cmdStr(cmd);
        std::istringstream iss(cmdStr);
        std::string token;

        // First token is the executable
        if (iss >> result.executable) {
            while (iss >> token) {
                result.args.push_back(token);
            }
        }
        return result;
    }

    // ── Hooked CreateSimpleProcess ──

    /**
     * CreateSimpleProcess signature (from libtier0_s.so):
     *   int CreateSimpleProcess(const char* cmdLine, unsigned int a2, const char* a3)
     *
     * We intercept calls to spawn steamwebhelper and inject:
     *   --remote-debugging-port=8080  (CDP access via TCP, same port the project already uses)
     *   --remote-allow-origins=*      (allow cross-origin CDP connections)
     *   --enable-unsafe-extension-debugging
     *
     * On Linux, --remote-debugging-pipe is NOT usable because CreateSimpleProcess
     * doesn't give us control over child fd inheritance (CEF expects fd 3/4 as pipes).
     * Instead we use --remote-debugging-port which is lightweight when only applied
     * to steamwebhelper (not the main Steam process).
     */
    typedef int (*CreateSimpleProcess_t)(const char*, unsigned int, const char*);

    static int hooked_CreateSimpleProcess(const char* cmd, unsigned int a2, const char* a3)
    {
        auto orig = reinterpret_cast<CreateSimpleProcess_t>(g_trampoline);

        // Check if this is the steamwebhelper being launched
        std::string cmdStr(cmd ? cmd : "");
        if (cmdStr.find("steamwebhelper") == std::string::npos) {
            // Not steamwebhelper — pass through unchanged
            return orig(cmd, a2, a3);
        }

        g_pLog->info("Tier0Hook: Intercepted steamwebhelper launch\n");
        g_pLog->debug("Tier0Hook: Original cmd: %s\n", cmd);

        // Parse the command and inject our flags
        ParsedCmd parsed = parseCommand(cmd);

        // Add --remote-debugging-port=8080 for CDP access
        // This is the same port the existing WebSocket-based CDP injection uses
        parsed.ensureParamWithValue("--remote-debugging-port", "8080");

        // Allow cross-origin CDP connections
        parsed.ensureParamWithValue("--remote-allow-origins", "*");

        // Enable extension debugging (lighter than --cef-enable-debugging)
        parsed.ensureParam("--enable-unsafe-extension-debugging");

        // Build the modified command
        std::string newCmd = parsed.build();
        g_pLog->info("Tier0Hook: Modified cmd: %s\n", newCmd.c_str());

        // Call original with modified command
        // Use thread_local to keep the string alive for the duration of the call
        static thread_local std::string s_modifiedCmd;
        s_modifiedCmd = newCmd;
        return orig(s_modifiedCmd.c_str(), a2, a3);
    }

    // ── Public API ──

    bool install()
    {
        if (g_hookInstalled) return true;

        // Find libtier0_s.so using libmem (reads /proc/self/maps directly).
        // We CANNOT use dlopen/dlsym here because LD_AUDIT modules live in a
        // separate linker namespace — dlopen(RTLD_NOLOAD) won't find libraries
        // loaded by the auditee (Steam). LM_FindModule bypasses this.
        lm_module_t tier0Mod{};
        if (!LM_FindModule("libtier0_s.so", &tier0Mod)) {
            g_pLog->warn("Tier0Hook: libtier0_s.so not found in /proc/self/maps\n");
            return false;
        }

        g_pLog->info("Tier0Hook: Found libtier0_s.so at %p (size=%zu, path=%s)\n",
                     reinterpret_cast<void*>(tier0Mod.base), tier0Mod.size, tier0Mod.path);

        // Look up the CreateSimpleProcess symbol from the module's ELF symbol table
        g_createSimpleProcessAddr = LM_FindSymbolAddress(&tier0Mod, "CreateSimpleProcess");
        if (g_createSimpleProcessAddr == LM_ADDRESS_BAD) {
            g_pLog->warn("Tier0Hook: CreateSimpleProcess symbol not found in libtier0_s.so\n");
            return false;
        }

        g_pLog->info("Tier0Hook: Found CreateSimpleProcess at %p\n",
                     reinterpret_cast<void*>(g_createSimpleProcessAddr));

        // Install the detour using libmem (same pattern as other hooks in the project)
        g_hookSize = LM_HookCode(
            g_createSimpleProcessAddr,
            reinterpret_cast<lm_address_t>(&hooked_CreateSimpleProcess),
            &g_trampoline
        );

        if (g_hookSize == 0 || g_trampoline == LM_ADDRESS_BAD) {
            g_pLog->warn("Tier0Hook: LM_HookCode failed for CreateSimpleProcess\n");
            return false;
        }

        // CRITICAL: Fix the PIC thunk call in the trampoline.
        // 32-bit Steam libraries use `call __x86.get_pc_thunk.bx` near the start
        // to set EBX = GOT base. LM_HookCode copies this into the trampoline, but
        // the relative call target becomes wrong because the trampoline is at a
        // different address. fixPICThunkCall patches it to load the correct EBX.
        MemHlp::fixPICThunkCall("CreateSimpleProcess", g_createSimpleProcessAddr, g_trampoline);

        g_hookInstalled = true;
        g_pLog->info("Tier0Hook: CreateSimpleProcess hooked successfully (size=%zu)\n", g_hookSize);
        return true;
    }

    void remove()
    {
        if (!g_hookInstalled) return;

        if (g_createSimpleProcessAddr != LM_ADDRESS_BAD && g_hookSize > 0) {
            LM_UnhookCode(g_createSimpleProcessAddr, g_trampoline, g_hookSize);
            g_pLog->info("Tier0Hook: CreateSimpleProcess hook removed\n");
        }

        g_hookInstalled = false;
        g_createSimpleProcessAddr = LM_ADDRESS_BAD;
        g_trampoline = LM_ADDRESS_BAD;
        g_hookSize = 0;
    }

    bool isHookInstalled()
    {
        return g_hookInstalled;
    }
}
