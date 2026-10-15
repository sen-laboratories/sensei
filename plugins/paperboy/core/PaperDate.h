/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <ctime>
#include <string>

namespace paperboy {

/**
 * @brief The moment (UTC, midnight) of a date written "YYYY-MM-DD", "YYYY-MM" or "YYYY"; the missing parts are the first.
 * @return -1 for anything else
 */
time_t ParseDate(const std::string& date);

}	// namespace paperboy
