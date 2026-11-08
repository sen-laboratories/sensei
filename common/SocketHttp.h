/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <SupportDefs.h>

#include <string>

/**
 * @file SocketHttp.h
 * @brief The HTTP client over a socket now lives in sento (`<sen/SenSocketHttp.h>`, header only, for every program of SEN); this keeps the names that the plugins use.
 */
#include <sen/SenSocketHttp.h>

namespace sensei {

using sen::http::NeedsSocketClient;
using sen::http::SocketGet;

}	// namespace sensei
