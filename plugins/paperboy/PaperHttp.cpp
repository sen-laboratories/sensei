/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperHttp.h"

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
	delete fSession;
}


status_t
HttpClient::Get(const std::string& url, std::string* body, const HttpOptions& options)
{
	// (a host that the client of Haiku could not read before is not asked twice)
	BUrl target(url.c_str(), true);
	if (target.HasHost() && fTolerantHosts.count(target.Host().String()) > 0) {
		bigtime_t wait = fLastRequest + options.minInterval - system_time();
		if (wait > 0)
			snooze(wait);
		fLastRequest = system_time();
		return _GetTolerant(url, body, options);
	}

	// made on the first request: a run that needs no network (identify) never starts the thread of the session
	if (fSession == NULL)
		fSession = new BHttpSession();

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
			// (the answer could not be read: the client is strict about the format of the headers)
			spdlog::info("request to {} failed in the client of Haiku ({}), trying a socket", url, error.Message());
			if (target.HasHost())
				fTolerantHosts.insert(target.Host().String());
			return _GetTolerant(url, body, options);
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


namespace {

std::string
Lower(std::string text)
{
	for (char& c : text)
		c = (char) std::tolower((unsigned char) c);
	return text;
}

/** the value of a header of a block of headers ("name: value" lines), "" if it is not there (the empty value is no reason to stop: that is what this is for) */
std::string
HeaderValue(const std::string& headers, const std::string& name)
{
	const std::string wanted = Lower(name) + ":";
	size_t from = 0;
	while (from < headers.size()) {
		size_t end = headers.find("\r\n", from);
		if (end == std::string::npos)
			end = headers.size();
		const std::string line = headers.substr(from, end - from);
		if (Lower(line.substr(0, wanted.size())) == wanted) {
			size_t start = wanted.size();
			while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
				start++;
			return line.substr(start);
		}
		from = end + 2;
	}
	return "";
}

/** the body of a chunked answer (size in hex, line break, data, line break... and a chunk of size 0); false if it is cut off */
bool
Dechunk(const std::string& raw, std::string* body)
{
	size_t at = 0;
	body->clear();
	while (at < raw.size()) {
		size_t lineEnd = raw.find("\r\n", at);
		if (lineEnd == std::string::npos)
			return false;
		const size_t size = std::strtoul(raw.substr(at, lineEnd - at).c_str(), nullptr, 16);
		at = lineEnd + 2;
		if (size == 0)
			return true;
		if (at + size > raw.size())
			return false;
		body->append(raw, at, size);
		at += size + 2;
	}
	return false;
}

}	// namespace


status_t
HttpClient::_GetTolerant(const std::string& url, std::string* body, const HttpOptions& options, int redirects)
{
	BUrl address(url.c_str(), true);
	if (!address.IsValid() || !address.HasHost())
		return B_BAD_VALUE;
	const bool secure = address.Protocol() == "https";
	if (!secure && address.Protocol() != "http")
		return B_BAD_VALUE;
	const int port = address.HasPort() ? address.Port() : (secure ? 443 : 80);

	BString target(address.HasPath() ? address.Path() : BString("/"));
	if (address.HasRequest()) {
		BString query(address.Request());
		if (query.ByteAt(0) == '?')
			query.Remove(0, 1);
		target << "?" << query;
	}

	BNetworkAddress peer(address.Host().String(), (uint16) port);
	BSecureSocket secureSocket;
	BSocket plainSocket;
	BSocket* socket = secure ? &secureSocket : &plainSocket;
	status_t status = socket->Connect(peer, kTimeout);
	if (status != B_OK) {
		spdlog::warn("could not connect to {}: {}", address.Host().String(), strerror(status));
		return status;
	}
	socket->SetTimeout(kTimeout);

	std::string request = std::string("GET ") + target.String() + " HTTP/1.1\r\nHost: " + address.Host().String()
		+ (address.HasPort() ? ":" + std::to_string(port) : "") + "\r\nUser-Agent: " + fUserAgent + "\r\nAccept: " + options.accept
		+ "\r\nConnection: close\r\n\r\n";
	for (size_t sent = 0; sent < request.size();) {
		ssize_t count = socket->Write(request.data() + sent, request.size() - sent);
		if (count <= 0)
			return count < 0 ? (status_t) count : B_IO_ERROR;
		sent += (size_t) count;
	}

	// the whole answer: it ends when the server closes the connection
	std::string raw;
	char buffer[16384];
	for (;;) {
		ssize_t count = socket->Read(buffer, sizeof(buffer));
		if (count == 0)
			break;
		if (count < 0) {
			if (raw.empty())
				return (status_t) count;
			break;		// (a secure connection that is closed without the notice: what was read is what there is)
		}
		raw.append(buffer, (size_t) count);
		if (raw.size() > kMaxAnswer)
			return B_NO_MEMORY;
	}
	socket->Disconnect();

	const size_t headEnd = raw.find("\r\n\r\n");
	if (headEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0)
		return B_BAD_DATA;
	const std::string head = raw.substr(0, headEnd);
	const size_t statusLineEnd = head.find("\r\n");
	const std::string statusLine = head.substr(0, statusLineEnd);
	const std::string headers = statusLineEnd == std::string::npos ? "" : head.substr(statusLineEnd + 2);
	const size_t space = statusLine.find(' ');
	const int code = space == std::string::npos ? 0 : std::atoi(statusLine.c_str() + space + 1);

	if (code >= 300 && code < 400 && !HeaderValue(headers, "Location").empty()) {
		if (redirects >= kMaxRedirections)
			return B_ERROR;
		BUrl next(address, BString(HeaderValue(headers, "Location").c_str()));
		return _GetTolerant(next.UrlString().String(), body, options, redirects + 1);
	}
	if (code == 404)
		return B_ENTRY_NOT_FOUND;
	if (code == 401 || code == 403)
		return B_NOT_ALLOWED;
	if (code == 429 || code == 503)
		return B_BUSY;
	if (code < 200 || code >= 300) {
		spdlog::warn("HTTP {} from {}", code, url);
		return B_ERROR;
	}

	const std::string content = raw.substr(headEnd + 4);
	if (Lower(HeaderValue(headers, "Transfer-Encoding")).find("chunked") != std::string::npos)
		return Dechunk(content, body) ? B_OK : B_BAD_DATA;
	const std::string length = HeaderValue(headers, "Content-Length");
	if (!length.empty() && std::strtoul(length.c_str(), nullptr, 10) < content.size())
		body->assign(content, 0, std::strtoul(length.c_str(), nullptr, 10));
	else
		*body = content;
	return B_OK;
}
