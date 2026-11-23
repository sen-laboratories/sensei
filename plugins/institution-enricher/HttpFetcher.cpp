/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "HttpFetcher.h"

#include <FindDirectory.h>
#include <Path.h>

#include <stdio.h>
#include <string.h>

#include "../../common/SocketHttp.h"

namespace campus {

static const long kMinGapMs = 300;
static const bigtime_t kTimeout = 20 * 1000000LL;

/** the key is part of the address that is asked, not of the one that is kept: a new key does not make the answers old */
static std::string
WithoutKey(std::string url)
{
	const size_t key = url.find("&api_key=");
	if (key == std::string::npos)
		return url;
	const size_t end = url.find('&', key + 1);
	url.erase(key, end == std::string::npos ? std::string::npos : end - key);
	return url;
}

HttpFetcher::HttpFetcher(const char* userAgent, bool debug)
	:
	fUserAgent(userAgent),
	fCache(NULL),
	fLimiter(NULL),
	fDebug(debug)
{
	BPath cachePath;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cachePath) == B_OK && cachePath.Append("sen/institution-enricher") == B_OK) {
		fCache = new sensei::DiskCache(cachePath.Path());
		BPath stampPath(cachePath);
		stampPath.Append("rate_stamp");
		fLimiter = new sensei::RateLimiter(stampPath.Path(), kMinGapMs);
	}
}


HttpFetcher::~HttpFetcher()
{
	delete fLimiter;
	delete fCache;
}


bool
HttpFetcher::Get(const std::string& url, long maxAgeSeconds, std::string* body, int* httpStatus)
{
	if (body == NULL)
		return false;
	if (httpStatus != NULL)
		*httpStatus = 0;

	const std::string cacheKey = WithoutKey(url);

	if (fCache != NULL && fCache->Get(cacheKey, body, maxAgeSeconds)) {
		if (fDebug)
			printf("kept answer for %s\n", cacheKey.c_str());
		if (httpStatus != NULL)
			*httpStatus = 200;
		return true;
	}

	if (fLimiter != NULL) {
		const long waited = fLimiter->Wait();
		if (fDebug && waited > 0)
			printf("waited %ld ms\n", waited);
	}
	if (fDebug)
		printf("asking %s\n", cacheKey.c_str());

	int status = 0;
	body->clear();
	const status_t result = sensei::SocketGet(url, fUserAgent, "application/json", body, &status, kTimeout);
	if (httpStatus != NULL)
		*httpStatus = status;
	if (result != B_OK) {
		if (fDebug)
			printf("  no answer (%s, status %d)\n", strerror(result), status);
		body->clear();
		return false;
	}
	if (fCache != NULL && !body->empty())
		fCache->Put(cacheKey, *body);
	return true;
}

}	// namespace campus
