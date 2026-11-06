/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <SupportDefs.h>

#include <string>

/**
 * @file SocketHttp.h
 * @brief A small HTTP/1.1 client over a plain socket, for the servers that the HTTP client of Haiku (BHttpSession) cannot talk to because of a bug in it.
 *
 * Wikimedia (Wikidata, Commons, Wikipedia and their pictures) sends `x-analytics:` with every answer: a header with an EMPTY value. That is valid HTTP: RFC 9110 defines
 * `field-value = *field-content`, which may be empty. `BHttpFields` of Haiku (libnetservices2, `HttpFields.cpp`) rejects an empty value and throws `InvalidInput` on a thread of the session,
 * and the program ends; it cannot be caught. This client makes the same GET over a socket (BSecureSocket for https), reads the answer to the end of the connection and reads only the headers
 * that it needs (the status, `Content-Length`, `Transfer-Encoding`, `Location`), so a header that it does not need cannot matter. It was Paperboy's and is here for every plugin;
 * BaseEnricher uses it for the hosts of Wikimedia (see NeedsSocketClient()). It can go when Haiku accepts an empty value.
 */
namespace sensei {

/** Whether the host is asked over a socket from the start, because the client of Haiku cannot read its answers: the hosts of Wikimedia (wikimedia.org, wikipedia.org, wikidata.org and what is below them). */
bool NeedsSocketClient(const std::string& host);

/**
 * One GET. Redirects are followed (at most five).
 * @param accept the value of the header `Accept`
 * @param httpStatus gets the HTTP status of the last answer (0 if there was none); may be NULL
 * @return B_OK for a 2xx answer; B_ENTRY_NOT_FOUND for 404; B_NOT_ALLOWED for 401 and 403; B_BUSY for 429 and 503; B_ERROR for any other status; B_BAD_VALUE for an address that is not
 *         http or https; the error of the network (B_TIMED_OUT, ...); B_BAD_DATA for an answer that is no HTTP answer or is cut off; B_NO_MEMORY for an answer of more than 32 MiB
 */
status_t SocketGet(const std::string& url, const std::string& userAgent, const char* accept, std::string* body, int* httpStatus = NULL);

}	// namespace sensei
