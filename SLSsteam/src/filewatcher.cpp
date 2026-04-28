#include "filewatcher.hpp"

#include "log.hpp"

#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>


//TODO: Investigate why gcc complains when put into CFileWatcher itself
void* watchLoop(void* args)
{
	auto watcher = reinterpret_cast<CFileWatcher*>(args);
	g_pLog->debug("Started FileWatcher %u\n", watcher->notifyFd);

	for(;;)
	{
		g_pLog->debug("Watching for changes...\n");

		char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
		ssize_t len = read(watcher->notifyFd, buffer, sizeof(buffer));
		if (len <= 0)
		{
			// If read was interrupted by a signal, try again
			if (len == -1 && errno == EINTR)
				continue;
			
			g_pLog->debug("inotify read error or closed: %d\n", errno);
			break;
		}

		for (char *ptr = buffer; ptr < buffer + len;
				ptr += sizeof(struct inotify_event) + reinterpret_cast<struct inotify_event *>(ptr)->len)
		{
			const struct inotify_event *event = reinterpret_cast<const struct inotify_event *>(ptr);
			
			if (watcher->fileFdMap.contains(event->wd))
			{
				g_pLog->debug("inotify %u(%s) -> %u (name: %s)\n", 
					event->wd, 
					watcher->fileFdMap[event->wd].c_str(), 
					event->mask,
					event->len ? event->name : "N/A");
				
				watcher->onModify(event->len ? event->name : nullptr);
			}
		}
	}

	return nullptr;
}

CFileWatcher::CFileWatcher(FileModifyEvent_t onModify)
{
	this->onModify = onModify;

	notifyFd = inotify_init();
	g_pLog->debug("Created notify fd %i\n", notifyFd);
}

CFileWatcher::~CFileWatcher()
{
	if (watchThread)
	{
		stop();
	}

	if (notifyFd != -1)
	{
		close(notifyFd);

		for(const auto& fd : fileFdMap)
		{
			if (fd.first == -1)
			{
				continue;
			}

			close(fd.first);
		}
	}
}

bool CFileWatcher::addWatch(const char* path, uint32_t mask)
{
	int fd = inotify_add_watch(notifyFd, path, mask);
	if (fd == -1)
	{
		return false;
	}

	fileFdMap[fd] = path;
	g_pLog->debug("Added %s (mask %u) to FileWatcher %i\n", path, mask, notifyFd);
	return fd != -1;
}

bool CFileWatcher::start()
{
	int code = pthread_create(&watchThread, nullptr, &watchLoop, this);
	return code == 0;
}

void CFileWatcher::stop()
{
	pthread_cancel(watchThread);
}
