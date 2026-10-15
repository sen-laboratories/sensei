/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperApi.h"

#include <cstring>

namespace paperboy {

namespace {

const char* const kOpenAlex = "https://api.openalex.org/works";
const char* const kSelectList = "id,doi,title,publication_year";

std::string
WithMailto(const std::string& url, const std::string& mailto)
{
	if (mailto.empty())
		return url;
	return url + (url.find('?') == std::string::npos ? '?' : '&') + "mailto=" + UrlEncode(mailto);
}

}	// namespace


std::string
UrlEncode(const std::string& text, const char* keep)
{
	static const char* const kHex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '.' || c == '_' || c == '~' || (c != '\0' && std::strchr(keep, c) != nullptr);
		if (plain) {
			out += (char) c;
		} else {
			out += '%';
			out += kHex[c >> 4];
			out += kHex[c & 15];
		}
	}
	return out;
}


std::string
OpenAlexWorkUrl(const std::string& doi, const std::string& mailto)
{
	// the DOI stays readable: its slashes belong to the path, and ;:() are legal there
	return WithMailto(std::string(kOpenAlex) + "/https://doi.org/" + UrlEncode(doi, "/;:()"), mailto);
}


std::string
OpenAlexSearchUrl(const std::string& title, const std::string& mailto)
{
	std::string clean;
	for (char c : title)
		clean += (c == ',' || c == '|' || c == ':' || c == '\n' || c == '\r') ? ' ' : c;
	return WithMailto(std::string(kOpenAlex) + "?search=" + UrlEncode(clean) + "&per-page=5&select=" + kSelectList, mailto);
}


std::string
OpenAlexBatchUrl(const std::vector<std::string>& ids, const std::string& mailto)
{
	std::string filter;
	size_t count = 0;
	for (const std::string& id : ids) {
		if (count == kBatchSize)
			break;
		filter += (filter.empty() ? "" : "|") + id;
		count++;
	}
	return WithMailto(std::string(kOpenAlex) + "?filter=openalex:" + UrlEncode(filter) + "&per-page="
		+ std::to_string(count) + "&select=" + kSelectList, mailto);
}


std::string
UnpaywallUrl(const std::string& doi, const std::string& email)
{
	return "https://api.unpaywall.org/v2/" + UrlEncode(doi, "/;:()") + "?email=" + UrlEncode(email);
}


std::string
UserAgent(const std::string& email)
{
	return email.empty() ? "paperboy/0.1 (SEN Labs; sen-labs.org)" : "paperboy/0.1 (mailto:" + email + ")";
}

}	// namespace paperboy
