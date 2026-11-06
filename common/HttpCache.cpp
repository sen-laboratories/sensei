/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "HttpCache.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace sensei {

namespace {

/** makes the folder and the folders above it; true if it is there */
bool
MakeFolders(const std::string& path)
{
	if (path.empty())
		return false;
	struct stat info;
	if (::stat(path.c_str(), &info) == 0)
		return S_ISDIR(info.st_mode);
	const size_t slash = path.find_last_of('/');
	if (slash != std::string::npos && slash > 0 && !MakeFolders(path.substr(0, slash)))
		return false;
	return ::mkdir(path.c_str(), 0755) == 0 || ::stat(path.c_str(), &info) == 0;
}

long long
NowMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}	// namespace


DiskCache::DiskCache(const std::string& folder)
	:
	fFolder(folder)
{
}


std::string
DiskCache::FileName(const std::string& key)
{
	// FNV-1a, 64 bit
	uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : key) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	char buffer[20];
	std::snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)hash);
	return buffer;
}


bool
DiskCache::Get(const std::string& key, std::string* body, long maxAgeSeconds) const
{
	if (fFolder.empty())
		return false;
	const std::string path = fFolder + "/" + FileName(key);
	struct stat info;
	if (::stat(path.c_str(), &info) != 0)
		return false;
	if (maxAgeSeconds >= 0 && NowMilliseconds() / 1000 - (long long)info.st_mtime > maxAgeSeconds)
		return false;
	std::ifstream file(path, std::ios::binary);
	std::stringstream content;
	content << file.rdbuf();
	const std::string text = content.str();
	const size_t newline = text.find('\n');
	if (newline == std::string::npos || text.compare(0, newline, key) != 0)
		return false;
	*body = text.substr(newline + 1);
	return true;
}


void
DiskCache::Put(const std::string& key, const std::string& body) const
{
	if (fFolder.empty() || !MakeFolders(fFolder))
		return;
	const std::string path = fFolder + "/" + FileName(key);
	const std::string temporary = path + ".part";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
			return;
		file << key << '\n' << body;
	}
	std::rename(temporary.c_str(), path.c_str());		// (an answer is whole or not there)
}


RateLimiter::RateLimiter(const std::string& stampFile, long minMilliseconds)
	:
	fStampFile(stampFile),
	fMinMilliseconds(minMilliseconds)
{
}


long
RateLimiter::Wait()
{
	long waited = 0;
	std::ifstream in(fStampFile);
	long long last = 0;
	if (in)
		in >> last;
	const long long now = NowMilliseconds();
	if (last > 0 && now >= last && now - last < fMinMilliseconds) {
		waited = (long)(fMinMilliseconds - (now - last));
		std::this_thread::sleep_for(std::chrono::milliseconds(waited));
	}
	const size_t slash = fStampFile.find_last_of('/');
	if (slash != std::string::npos && slash > 0)
		MakeFolders(fStampFile.substr(0, slash));
	std::ofstream out(fStampFile, std::ios::trunc);
	if (out)
		out << NowMilliseconds();
	return waited;
}

}	// namespace sensei
