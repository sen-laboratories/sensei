/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

/**
 * What the policy of the public Nominatim service asks of a program that uses it: the answers are kept (the same question is not asked twice), and there is at most one request per
 * second, however many instances of the plugin run (every enrichment is its own process, so the time of the last request is a file). Plain C++ (POSIX files), no Haiku.
 */
namespace place {

/** Answers on disk, one file for each address: the first line is the address (a collision of the name is no hit), the rest is the answer. */
class DiskCache {
public:
	/** @p folder is made when the first answer is kept; an empty name keeps nothing. */
	explicit DiskCache(const std::string& folder);

	/** The answer for the address, if there is one that is not older than @p maxAgeSeconds. */
	bool Get(const std::string& key, std::string* body, long maxAgeSeconds) const;
	/** Keeps an answer (replaces the one that is there). Nothing is kept if the folder cannot be made. */
	void Put(const std::string& key, const std::string& body) const;
	/** The name of the file for an address: a hash of it as 16 hexadecimal digits. */
	static std::string FileName(const std::string& key);

private:
	std::string	fFolder;
};

/** At least a given time between two requests, over all processes. */
class RateLimiter {
public:
	/** @p stampFile holds the time of the last request (its folder is made when needed), @p minMilliseconds is the least time between two. */
	RateLimiter(const std::string& stampFile, long minMilliseconds);

	/** Waits until the time has passed since the last request, then records that a request is made now. Returns how long it waited, in milliseconds. */
	long Wait();

private:
	std::string	fStampFile;
	long		fMinMilliseconds;
};

}	// namespace place
