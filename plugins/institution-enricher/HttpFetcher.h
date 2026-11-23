/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <string>

#include "../../common/HttpCache.h"

namespace campus {

/**
 * What the enricher asks ROR and OpenAlex with: the SEN socket client, the answers kept on disk (`~/config/cache/sen/institution-enricher/`, as long as the caller says) and a polite
 * time between two requests.
 */
class HttpFetcher {
public:
	HttpFetcher(const char* userAgent, bool debug = false);
	~HttpFetcher();

	void SetDebug(bool debug) { fDebug = debug; }

	/** True, with the body, for an answer of the service (2xx) or a kept one that is not older than @p maxAgeSeconds; the status is what the service said (0: no answer). */
	bool Get(const std::string& url, long maxAgeSeconds, std::string* body, int* httpStatus);

private:
	std::string				fUserAgent;
	sensei::DiskCache*		fCache;
	sensei::RateLimiter*	fLimiter;
	bool					fDebug;
};

}	// namespace campus
