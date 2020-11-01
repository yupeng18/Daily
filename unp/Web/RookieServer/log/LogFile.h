#pragma once

#include "../base/Mutex.h"
#include "FileUtil.h"
#include "../base/MemoryPool.h"

class LogFile:noncopyable
{
private:
	void append_unlocked(const char *logline, int len);
	const std::string basename;
	const int flushEveryN;
	int count;
	MutexLock mutex;
	UP_FileUtil file;

public:
	LogFile(const std::string& Basename, int FlushEveryN = 1024);
	~LogFile();
	void append(const char *logline, int len);
	void flush();
	bool rollFile();
};
