#pragma once

#include <cstdarg>
#include <cstdint>
#include <fstream>
#include <memory>
#include <shared_mutex>
#include <unordered_set>

#define LOG_TRACE(fmt, ...) g_pLog->trace(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_TRACE_ONCE(fmt, ...) g_pLog->traceOnce(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_ONCE(fmt, ...) g_pLog->once(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_DEBUG(fmt, ...) g_pLog->debug(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG_ONCE(fmt, ...) g_pLog->debugOnce(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_WARN(fmt, ...) g_pLog->warn(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(fmt, ...) g_pLog->error(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(fmt, ...) g_pLog->info(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_NOTIFY(fmt, ...) g_pLog->notify(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_NOTIFYLONG(fmt, ...) g_pLog->notifyLong(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_NOTIFYWARN(fmt, ...) g_pLog->notifyWarn(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_NOTIFYERROR(fmt, ...) g_pLog->notifyError(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_API(fmt, ...) g_pLog->api(__FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_CUSTOM(lvl, fmt, ...) g_pLog->custom(lvl, __FILE__, __FUNCTION__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

constexpr unsigned int ELogLevelCount = 9;

enum ELogLevel : uint32_t
{
	k_ELogLevelTrace = 1 << 0, //Tracing for debug
	k_ELogLevelOnce = 1 << 1, //Only log once
	k_ELogLevelDebug = 1 << 2, //Debugging statements
	k_ELogLevelWarn = 1 << 3, //Something went wrong but it's not terrible
	k_ELogLevelError = 1 << 4, //Something went wrong and it's terrible/can't be recovered from. Function failed
	k_ELogLevelInfo = 1 << 5, //Log for users/external tools
	k_ELogLevelNotifyShort = 1 << 6,
	k_ELogLevelNotifyLong = 1 << 7,

	k_ELogLevelAPI = 1 << 8 //Used by API. Not togglable via config, gets set by API yes/no
};

std::string ELogLevel_ToString(unsigned int lvlFlags);

class CLog
{
	std::ofstream ofstream;
	std::unordered_set<std::string> msgHist {};
	std::shared_mutex mutex;

	bool shouldNotify(const unsigned int flags);
	std::string buildNotification(const unsigned int flags, const char* msg);
	void __log(const unsigned int flags, const char* file, const char* function, const int line, const char* msg, const va_list& vArgs);

public:
	std::string path;

	CLog(const char* path);
	~CLog();

	//Do not include config.hpp in this header, otherwise things will break :) (proly due to recursive inclusion)
	__attribute__((__format__(__printf__, 5, 6)))
	void trace(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void traceOnce(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void once(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void debug(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void debugOnce(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void warn(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void error(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void info(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void notify(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void notifyLong(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void notifyWarn(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void notifyError(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 5, 6)))
	void api(const char* file, const char* function, const int line, const char* msg, ...);
	__attribute__((__format__(__printf__, 6, 7)))
	void custom(const unsigned int flags, const char* file, const char* function, const int line, const char* msg, ...);

	static CLog* createDefaultLog();
};

extern std::unique_ptr<CLog> g_pLog;
