/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperHttp.h"

#include <cstdlib>

#include <DataIO.h>
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
constexpr bigtime_t kMinInterval = 150 * 1000LL;		// OpenAlex: ten requests a second at most
constexpr bigtime_t kDefaultBackoff = 2 * 1000000LL;
constexpr bigtime_t kMaxBackoff = 10 * 1000000LL;
constexpr int kMaxAttempts = 3;
constexpr uint8 kMaxRedirections = 5;

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
	delete fSession;
}


status_t
HttpClient::Get(const std::string& url, std::string* body)
{
	// made on the first request: a run that needs no network (identify) never starts the thread of the session
	if (fSession == NULL)
		fSession = new BHttpSession();

	for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
		bigtime_t wait = fLastRequest + kMinInterval - system_time();
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
		fields.AddField("Accept"sv, "application/json"sv);
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
			spdlog::warn("request to {} failed: {}", url, error.Message());
			return B_ERROR;
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
