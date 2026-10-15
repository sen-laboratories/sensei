/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include <SupportDefs.h>

namespace BPrivate { namespace Network { class BHttpSession; } }

/**
 * A polite client for the two services: it names itself (user agent with an address, if there is one), waits between requests
 * (OpenAlex allows ten a second) and waits and tries again when it is told to slow down (HTTP 429).
 */
class HttpClient {
public:
								HttpClient(const std::string& userAgent);
								~HttpClient();

	/**
	 * @brief Fetches a document.
	 * @return B_OK; B_ENTRY_NOT_FOUND if the service does not know the document (404); B_BUSY if it kept asking to slow down;
	 *         B_TIMED_OUT or another error from the network, B_ERROR for any other HTTP status
	 */
	status_t					Get(const std::string& url, std::string* body);

private:
	BPrivate::Network::BHttpSession*	fSession;
	std::string					fUserAgent;
	bigtime_t					fLastRequest;
};
