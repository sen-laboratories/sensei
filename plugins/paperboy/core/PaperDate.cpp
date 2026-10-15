/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperDate.h"

#include <cctype>

namespace paperboy {

namespace {

/** reads exactly @p digits digits at @p pos; -1 if they are not there */
int
Digits(const std::string& s, size_t pos, size_t digits)
{
	if (pos + digits > s.size())
		return -1;
	int value = 0;
	for (size_t i = 0; i < digits; i++) {
		if (!std::isdigit((unsigned char) s[pos + i]))
			return -1;
		value = value * 10 + (s[pos + i] - '0');
	}
	return value;
}

int
DaysInMonth(int year, int month)
{
	static const int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return month == 2 && leap ? 29 : kDays[month - 1];
}

}	// namespace


time_t
ParseDate(const std::string& date)
{
	int year = Digits(date, 0, 4);
	if (year < 1000)
		return -1;
	int month = 1, day = 1;
	if (date.size() > 4) {
		if (date[4] != '-' || (month = Digits(date, 5, 2)) < 1 || month > 12)
			return -1;
		if (date.size() > 7) {
			if (date[7] != '-' || (day = Digits(date, 8, 2)) < 1 || day > DaysInMonth(year, month) || date.size() != 10)
				return -1;
		} else if (date.size() != 7) {
			return -1;
		}
	}

	// days since 1970-01-01 of a date of the proleptic Gregorian calendar (Howard Hinnant's days_from_civil);
	// timegm() is not in strict C++20 and not on every system
	int y = month <= 2 ? year - 1 : year;
	int era = y / 400;
	int yearOfEra = y - era * 400;
	int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
	long long days = (long long) era * 146097 + dayOfEra - 719468;
	return (time_t) (days * 86400);
}

}	// namespace paperboy
