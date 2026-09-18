#include "../src/feats/autoupdate.hpp"
#include "../src/log.hpp"
#include <iostream>
#include <cstdarg>

// Provide minimal CLog stub so test_autoupdate does not depend on full log.cpp or g_config
void CLog::trace(const char*, const char*, const int, const char*, ...) {}
void CLog::traceOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::once(const char*, const char*, const int, const char*, ...) {}
void CLog::debug(const char*, const char*, const int, const char*, ...) {}
void CLog::debugOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::warn(const char*, const char*, const int, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void CLog::error(const char*, const char*, const int, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void CLog::info(const char*, const char*, const int, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void CLog::notify(const char*, const char*, const int, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void CLog::notifyLong(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyWarn(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyError(const char*, const char*, const int, const char*, ...) {}
void CLog::api(const char*, const char*, const int, const char*, ...) {}
void CLog::custom(const unsigned int, const char*, const char*, const int, const char*, ...) {}

CLog::CLog(const char* p) : path(p ? p : "") {}
CLog::~CLog() {}
CLog* CLog::createDefaultLog() { return new CLog("/dev/null"); }

std::unique_ptr<CLog> g_pLog = std::make_unique<CLog>("/dev/null");

int main(int argc, char** argv)
{
    std::cout << "[TEST_AUTOUPDATE] Running AutoUpdate::checkAndPromptSync()...\n";
    AutoUpdate::checkAndPromptSync();
    std::cout << "[TEST_AUTOUPDATE] Completed successfully.\n";
    return 0;
}
