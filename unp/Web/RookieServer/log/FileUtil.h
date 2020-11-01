#pragma once

#include "../base/noncopyable.h"
#include <fstream>
#include <memory>
#include "../base/MemoryPool.h"

#define BUFFERSIZE 64*1024

class FileUtil:noncopyable
{
private:
	int write(const char *logline, int len);
	FILE *fp;
	char buffer[BUFFERSIZE];	
	
public:
	explicit FileUtil(std::string filename);
	~FileUtil();
	void append(const char *logline, int len);
	void flush();
};

typedef std::unique_ptr<FileUtil, decltype(deleteElement<FileUtil>)*> UP_FileUtil;
