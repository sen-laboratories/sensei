/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperHttp.h"
#include "../../common/SharedHttpSession.h"
#include "../../common/SocketHttp.h"

#include <cctype>
#include <cstdlib>

#include <DataIO.h>
#include <NetworkAddress.h>
#include <SecureSocket.h>
#include <Socket.h>
#include <String.h>
#include <OS.h>
#include <Url.h>

#include <private/netservices2/ExclusiveBorrow.h>
#include <private/netservices2/HttpFields.h>
#include <private/netservices2/HttpRequest.h>
#include <private/netservices2/HttpResult.h>
#include <private/netservices2/HttpSession.h>
#include <private/netservices2/NetServicesDefs.h>

#include <spdlog/spdlog.h>

using namespace BPrivate::Network;
using namespace std::literals;

namespace {

constexpr bigtime_t kTimeout = 20 * 1000000LL;
constexpr bigtime_t kDefaultBackoff = 2 * 1000000LL;
constexpr bigtime_t kMaxBackoff = 10 * 1000000LL;
constexpr int kMaxRedirections = 5;
constexpr size_t kMaxAnswer = 32 * 1024 * 1024;

}	// namespace


HttpClient::HttpClient(const std::string& userAgent)
	:
	fSession(NULL),
	fUserAgent(userAgent),
	fLastRequest(0)
{
}


HttpClient::~HttpClient()
{
	// (the session of the process is not ours to delete, see SharedHttpSession.h)
}


status_t
HttpClient::Get(const std::string& url, std::string* body, const HttpOptions& options)
{
	// (a host that the client of Haiku could not read before is not asked twice)
	BUrl target(url.c_str(), true);
	if (target.HasHost() && fSocketHosts.count(target.Host().String()) > 0) {
		bigtime_t wait = fLastRequest + options.minInterval - system_time();
		if (wait > 0)
			snooze(wait);
		fLastRequest = system_time();
		return _GetOverSocket(url, body, options);
	}

	// made on the first request: a run that needs no network (identify) never starts the thread of the session
	if (fSession == NULL)
		fSession = sen::SharedHttpSession();

	for (int attempt = 1; attempt <= options.maxAttempts; attempt++) {
		bigtime_t wait = fLastRequest + options.minInterval - system_time();
		if (wait > 0)
			snooze(wait);
		fLastRequest = system_time();

		BUrl bUrl(url.c_str(), true);
		if (!bUrl.IsValid())
			return B_BAD_VALUE;

		BHttpRequest request(bUrl);
		request.SetTimeout(kTimeout);
		request.SetMaxRedirections(kMaxRedirections);
		// Fields() is a read-only view: change a copy and set it
		BHttpFields fields = request.Fields();
		fields.AddField("User-Agent"sv, fUserAgent);
		fields.AddField("Accept"sv, std::string_view(options.accept));
		request.SetFields(fields);

		auto content = make_exclusive_borrow<BMallocIO>();
		BHttpStatus status;
		bigtime_t backoff = kDefaultBackoff;
		spdlog::debug("GET {}", url);

		try {
			auto result = fSession->Execute(std::move(request), BBorrow<BDataIO>(content));
			// Status() blocks until the response is complete
			status = result.Status();
			if (status.code == 429) {
				const BHttpFields& answer = result.Fields();
				auto retry = answer.FindField("Retry-After"sv);
				if (retry != answer.end()) {
					long seconds = std::strtol(std::string(retry->Value()).c_str(), nullptr, 10);
					if (seconds > 0)
						backoff = seconds * 1000000LL;
				}
			}
			result.Body();		// synchronise with the borrowed buffer
		} catch (const BNetworkRequestError& error) {
			spdlog::warn("request to {} failed: {}", url, error.Message());
			return error.ErrorCode();
		} catch (const BError& error) {
			// (the answer could not be read by Haiku's client, e.g. a header with an empty value, which it wrongly refuses)
			spdlog::info("request to {} failed in Haiku's client ({}), trying a socket", url, error.Message());
			if (target.HasHost())
				fSocketHosts.insert(target.Host().String());
			return _GetOverSocket(url, body, options);
		}

		if (status.code >= 200 && status.code < 300) {
			try {
				body->assign(reinterpret_cast<const char*>(content->Buffer()), content->BufferLength());
			} catch (const BBorrowError&) {
				return B_ERROR;
			}
			return B_OK;
		}
		if (status.code == 404)
			return B_ENTRY_NOT_FOUND;
		if (status.code == 401 || status.code == 403) {
			spdlog::warn("HTTP {} from {}: {}", status.code, url, status.text.String());
			return B_NOT_ALLOWED;
		}
		if (status.code == 429 || status.code == 503) {
			if (attempt == options.maxAttempts) {
				spdlog::info("{} said {}", url, status.code);
				break;
			}
			if (backoff > kMaxBackoff)
				backoff = kMaxBackoff;
			spdlog::info("{} said {}, waiting {} s", url, status.code, (long) (backoff / 1000000));
			snooze(backoff);
			continue;
		}
		spdlog::warn("HTTP {} from {}: {}", status.code, url, status.text.String());
		return B_ERROR;
	}
	return B_BUSY;
}


status_t
HttpClient::_GetOverSocket(const std::string& url, std::string* body, const HttpOptions& options, int)
{
	// (the request over a socket is shared by the plugins: common/SocketHttp.h)
	int httpStatus = 0;
	status_t result = sensei::SocketGet(url, fUserAgent, options.accept, body, &httpStatus);
	if (result == B_ERROR)
		spdlog::warn("HTTP {} from {}", httpStatus, url);
	return result;
}
