/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <string>

#include "../../common/HttpCache.h"
#include "core/PersonEnricher.h"

namespace robin {

/**
 * What Robin asks the web with: the SEN socket client (it sends the header `Accept` that ActivityPub wants, follows redirects, and reads the hosts that Haiku's HTTP client cannot),
 * the answers kept on disk (`~/config/cache/sen/people-enricher/`, each as long as its caller says: a profile for a week, a feed for an hour), and a polite time between two requests, for
 * all programs that use it (the enricher and the navigator).
 */
class HttpFetcher {
public:
	HttpFetcher(const char* userAgent, bool debug = false);
	~HttpFetcher();

	void SetDebug(bool debug) { fDebug = debug; }

	/** True, with the body, for an answer of the service (2xx) or a kept one that is not older than @p maxAgeSeconds. */
	bool Get(const std::string& url, const char* accept, long maxAgeSeconds, std::string* body, int* httpStatus);

	/** For PersonEnricher. */
	PersonEnricher::HttpGetFunc Function();

private:
	std::string			fUserAgent;
	sensei::DiskCache*	fCache;
	sensei::RateLimiter*	fLimiter;
	bool				fDebug;
};

}	// namespace robin
