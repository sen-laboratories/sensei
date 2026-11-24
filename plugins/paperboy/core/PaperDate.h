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

/**
 * @brief The last second (UTC) of the period that a date names: "2019" is the end of 2019-12-31, "2019-03" of 2019-03-31, "2019-03-05" of that day. For the end of a stay at an
 * institution, which is not over before the period is.
 * @return -1 for anything else
 */
time_t ParseDateEnd(const std::string& date);

}	// namespace paperboy
