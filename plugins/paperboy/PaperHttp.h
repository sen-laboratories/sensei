/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <set>
#include <string>

#include <SupportDefs.h>

namespace BPrivate { namespace Network { class BHttpSession; } }

/**
 * A polite client for the two services: it names itself (user agent with an address, if there is one), waits between requests
 * (OpenAlex allows ten a second) and waits and tries again when it is told to slow down (HTTP 429).
 */
/** How a service wants to be asked: the others than OpenAlex are asked slowly and once (a refusal is no reason to wait for it: the next source is asked). */
struct HttpOptions {
	bigtime_t	minInterval = 150 * 1000LL;			///< between two requests of the client (OpenAlex: ten a second at most)
	const char*	accept = "application/json";
	int			maxAttempts = 3;					///< a 429 or 503 is answered with a pause and another attempt
};

class HttpClient {
public:
								HttpClient(const std::string& userAgent);
								~HttpClient();

	/**
	 * @brief Fetches a document.
	 * @return B_OK; B_ENTRY_NOT_FOUND if the service does not know the document (404); B_BUSY if it kept asking to slow down (or the daily budget is used up); B_NOT_ALLOWED for 401 and 403 (a wrong key);
	 *         B_TIMED_OUT or another error from the network, B_ERROR for any other HTTP status
	 */
	status_t					Get(const std::string& url, std::string* body, const HttpOptions& options = HttpOptions());

private:
	/**
	 * The same request over a plain socket (BSecureSocket for https), HTTP/1.1 with the answer read to the end of the connection. Haiku's own client refuses an
	 * answer that has a header with an empty value (Wikimedia sends `x-analytics:` with all of them: a bug of the client, RFC 9110 allows it), so the services of
	 * Wikimedia (Wikidata, Commons and their pictures) are read this way. It reads the headers it needs (the status, `Content-Length`, `Transfer-Encoding`,
	 * `Location`) and no more.
	 */
	status_t					_GetTolerant(const std::string& url, std::string* body, const HttpOptions& options, int redirects = 0);

	BPrivate::Network::BHttpSession*	fSession;
	std::string					fUserAgent;
	bigtime_t					fLastRequest;
	std::set<std::string>		fTolerantHosts;		///< the hosts whose answers Haiku's client refuses: they are asked over a socket from the start
};
