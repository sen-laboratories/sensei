/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "HttpFetcher.h"

#include <FindDirectory.h>
#include <Path.h>

#include <stdio.h>

#include "../../common/SocketHttp.h"

namespace robin {

static const long kMinGapMs = 500;
static const bigtime_t kTimeout = 20 * 1000000LL;

HttpFetcher::HttpFetcher(const char* userAgent, bool debug)
	:
	fUserAgent(userAgent),
	fCache(NULL),
	fLimiter(NULL),
	fDebug(debug)
{
	BPath cachePath;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cachePath) == B_OK && cachePath.Append("sen/people-enricher") == B_OK) {
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
HttpFetcher::Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body, int* httpStatus)
{
	if (body == NULL)
		return false;
	if (httpStatus != NULL)
		*httpStatus = 0;

	if (fCache != NULL && fCache->Get(url, body, maxAgeSeconds)) {
		if (fDebug)
			printf("kept answer for %s\n", url.c_str());
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
		printf("asking %s\n", url.c_str());

	int status = 0;
	body->clear();
	const status_t result = sensei::SocketGet(url, fUserAgent, accept, body, &status, kTimeout);
	if (httpStatus != NULL)
		*httpStatus = status;
	if (result != B_OK) {
		if (fDebug)
			printf("  no answer (%s, status %d)\n", strerror(result), status);
		body->clear();
		return false;
	}
	if (fCache != NULL && !body->empty())
		fCache->Put(url, *body);
	return true;
}


PersonEnricher::HttpGetFunc
HttpFetcher::Function()
{
	return [this](const std::string& url, const char* accept, long maxAge, std::string* body, int* status) {
		return Get(url, accept, maxAge, body, status);
	};
}

}	// namespace robin
