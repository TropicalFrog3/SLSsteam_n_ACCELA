#pragma once

#include <filesystem>
#include <pthread.h>
#include <sys/inotify.h>
#include <unordered_map>
#include <string>
#include <cstdint>

typedef void(*FileModifyEvent_t)(const char* filename);

class CFileWatcher
{
	pthread_t watchThread;

public:
	constexpr static int WATCH_MASK = IN_CLOSE_WRITE | IN_MOVED_TO;

	int notifyFd;
	std::unordered_map<int, std::string> fileFdMap;

	FileModifyEvent_t onModify;

	CFileWatcher(FileModifyEvent_t onModify);
	~CFileWatcher();

	bool addWatch(const char* path, uint32_t mask = 0x00000002); // IN_MODIFY
	bool start();
	void stop();
};

