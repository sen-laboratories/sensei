/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "IcsTime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace ics {

int64_t
DaysFromCivil(int year, int month, int day)
{
	year -= month <= 2;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yoe = (unsigned)(year - era * 400);
	const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}


void
CivilFromDays(int64_t days, int* year, int* month, int* day)
{
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned doe = (unsigned)(days - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	*year = (int)(y + (m <= 2));
	*month = (int)m;
	*day = (int)d;
}


bool
IsLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}


int
DaysInMonth(int year, int month)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month < 1 || month > 12)
		return 30;
	return month == 2 && IsLeapYear(year) ? 29 : days[month - 1];
}


int
DayOfWeek(int year, int month, int day)
{
	const int64_t days = DaysFromCivil(year, month, day);
	return (int)(((days % 7) + 7 + 4) % 7);		// (1970-01-01 was a Thursday)
}


int64_t
LocalSeconds(const DateTime& time)
{
	return DaysFromCivil(time.year, time.month, time.day) * 86400 + time.hour * 3600 + time.minute * 60 + time.second;
}


DateTime
FromLocalSeconds(int64_t seconds)
{
	DateTime time;
	int64_t days = seconds / 86400;
	int64_t rest = seconds % 86400;
	if (rest < 0) {
		rest += 86400;
		days--;
	}
	CivilFromDays(days, &time.year, &time.month, &time.day);
	time.hour = (int)(rest / 3600);
	time.minute = (int)((rest % 3600) / 60);
	time.second = (int)(rest % 60);
	time.valid = true;
	return time;
}


namespace {

std::string
Squeezed(const std::string& value)
{
	std::string out;
	for (char c : value) {
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			out += c;
	}
	return out;
}

bool
Digits(const std::string& text, size_t at, size_t count, int* number)
{
	if (at + count > text.size())
		return false;
	int value = 0;
	for (size_t i = at; i < at + count; i++) {
		if (!isdigit((unsigned char)text[i]))
			return false;
		value = value * 10 + (text[i] - '0');
	}
	*number = value;
	return true;
}

std::string
Upper(const std::string& text)
{
	std::string out = text;
	for (char& c : out)
		c = (char)toupper((unsigned char)c);
	return out;
}

}	// namespace


bool
ParseDateTime(const std::string& value, const std::string& tzid, bool dateOnly, DateTime* out)
{
	// the dashes and colons of the extended form are left out: `2026-11-16T09:30:00` is `20261116T093000`
	std::string text;
	for (char c : Squeezed(value)) {
		if (c != '-' && c != ':')
			text += c;
	}
	DateTime time;
	if (!Digits(text, 0, 4, &time.year) || !Digits(text, 4, 2, &time.month) || !Digits(text, 6, 2, &time.day))
		return false;
	if (time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31)
		return false;
	size_t at = 8;
	if (at < text.size() && (text[at] == 'T' || text[at] == 't')) {
		at++;
		if (!Digits(text, at, 2, &time.hour))
			return false;
		at += 2;
		if (Digits(text, at, 2, &time.minute))
			at += 2;
		if (Digits(text, at, 2, &time.second))
			at += 2;
		if (time.hour > 24 || time.minute > 59 || time.second > 60)
			return false;
		if (at < text.size() && (text[at] == 'Z' || text[at] == 'z'))
			time.utc = true;
		// (a fraction of a second, an offset: ignored)
		if (dateOnly) {
			time.hour = time.minute = time.second = 0;
			time.utc = false;
		}
	} else {
		time.dateOnly = true;
	}
	if (dateOnly)
		time.dateOnly = true;
	if (time.second == 60)
		time.second = 59;
	if (time.hour == 24) {
		// (24:00 is the end of the day: the next one begins)
		const DateTime next = FromLocalSeconds(LocalSeconds(time) - 24 * 3600 + 24 * 3600);
		time = DateTime(next);
		time.utc = text.find('Z') != std::string::npos || text.find('z') != std::string::npos;
	}
	time.valid = true;
	if (!time.utc)
		time.tzid = tzid;
	*out = time;
	return true;
}


bool
ParseDuration(const std::string& text, int64_t* seconds)
{
	const std::string value = Upper(Squeezed(text));
	size_t at = 0;
	bool negative = false;
	if (at < value.size() && (value[at] == '-' || value[at] == '+')) {
		negative = value[at] == '-';
		at++;
	}
	if (at >= value.size() || value[at] != 'P')
		return false;
	at++;
	int64_t total = 0;
	bool any = false, inTime = false;
	while (at < value.size()) {
		if (value[at] == 'T') {
			inTime = true;
			at++;
			continue;
		}
		size_t end = at;
		while (end < value.size() && isdigit((unsigned char)value[end]))
			end++;
		if (end == at || end >= value.size())
			return false;
		const int64_t number = strtoll(value.substr(at, end - at).c_str(), NULL, 10);
		switch (value[end]) {
			case 'W': total += number * 7 * 86400; break;
			case 'D': total += number * 86400; break;
			case 'H': total += number * 3600; break;
			case 'M': total += inTime ? number * 60 : number * 30 * 86400; break;
			case 'S': total += number; break;
			default: return false;
		}
		any = true;
		at = end + 1;
	}
	if (!any)
		return false;
	*seconds = negative ? -total : total;
	return true;
}


std::string
DateTimeText(const DateTime& time)
{
	char buffer[48];
	if (time.dateOnly)
		snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", time.year, time.month, time.day);
	else
		snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d", time.year, time.month, time.day, time.hour, time.minute, time.second);
	return buffer;
}


// ---- recurrence rules

namespace {

int
WeekdayOf(const std::string& code)
{
	static const char* names[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
	const std::string upper = Upper(code);
	for (int i = 0; i < 7; i++) {
		if (upper == names[i])
			return i;
	}
	return -1;
}

std::vector<std::string>
Split(const std::string& text, char separator)
{
	std::vector<std::string> parts;
	size_t at = 0;
	while (at <= text.size()) {
		size_t end = text.find(separator, at);
		if (end == std::string::npos)
			end = text.size();
		parts.push_back(text.substr(at, end - at));
		at = end + 1;
	}
	return parts;
}

std::vector<int>
Numbers(const std::string& text)
{
	std::vector<int> numbers;
	for (const std::string& part : Split(text, ',')) {
		if (!part.empty())
			numbers.push_back(atoi(part.c_str()));
	}
	return numbers;
}

/** `MO`, `2MO`, `-1FR`, `+1SU` */
bool
ParseWeekDay(const std::string& text, WeekDay* day)
{
	if (text.size() < 2)
		return false;
	const int weekday = WeekdayOf(text.substr(text.size() - 2));
	if (weekday < 0)
		return false;
	day->weekday = weekday;
	day->ordinal = text.size() > 2 ? atoi(text.substr(0, text.size() - 2).c_str()) : 0;
	return true;
}

/** vCalendar 1.0: `D1 #5`, `W2 MO TU #10`, `MP1 1+ MO #0`, `MD1 15 #3`, `YM1 6 #3`, `YD1 100 #2`; an end date instead of `#count` is possible */
bool
ParseOldRule(const std::string& text, Rule* rule)
{
	std::vector<std::string> words;
	{
		std::string word;
		for (char c : text) {
			if (c == ' ' || c == '\t' || c == ';') {
				if (!word.empty())
					words.push_back(word);
				word.clear();
			} else {
				word += c;
			}
		}
		if (!word.empty())
			words.push_back(word);
	}
	if (words.empty())
		return false;
	const std::string head = Upper(words[0]);
	size_t digits = head.size();
	while (digits > 0 && isdigit((unsigned char)head[digits - 1]))
		digits--;
	const std::string kind = head.substr(0, digits);
	rule->interval = digits < head.size() ? std::max(1, atoi(head.substr(digits).c_str())) : 1;
	if (kind == "D")
		rule->frequency = Frequency::Daily;
	else if (kind == "W")
		rule->frequency = Frequency::Weekly;
	else if (kind == "MP" || kind == "MD")
		rule->frequency = Frequency::Monthly;
	else if (kind == "YM" || kind == "YD")
		rule->frequency = Frequency::Yearly;
	else
		return false;
	for (size_t i = 1; i < words.size(); i++) {
		const std::string word = Upper(words[i]);
		if (word[0] == '#') {
			rule->count = atoi(word.c_str() + 1);
			continue;
		}
		WeekDay day;
		if (ParseWeekDay(word, &day)) {
			rule->byDay.push_back(day);
			continue;
		}
		// an ordinal of a week day (`1+ MO`, `2- FR`) or a day of the month (`15`, `1-`: the last) or a number of a month or day of the year
		int number = atoi(word.c_str());
		const char last = word[word.size() - 1];
		if (last == '-')
			number = -number;
		if (isdigit((unsigned char)word[0]) || word[0] == '-' || word[0] == '+') {
			if (kind == "MP") {
				// (the ordinal belongs to the week days that follow it)
				size_t j = i + 1;
				bool any = false;
				for (; j < words.size(); j++) {
					WeekDay each;
					if (!ParseWeekDay(Upper(words[j]), &each))
						break;
					each.ordinal = number;
					rule->byDay.push_back(each);
					any = true;
				}
				if (any)
					i = j - 1;
			} else if (kind == "MD") {
				rule->byMonthDay.push_back(number);
			} else if (kind == "YM") {
				rule->byMonth.push_back(number);
			} else if (kind == "YD") {
				rule->byYearDay.push_back(number);
			} else if (word.size() >= 8 && ParseDateTime(word, "", false, &rule->until)) {
				// (an end date)
			}
		}
	}
	return true;
}

}	// namespace


bool
ParseRule(const std::string& text, Rule* rule)
{
	Rule result;
	std::string trimmed = text;
	while (!trimmed.empty() && isspace((unsigned char)trimmed.back()))
		trimmed.pop_back();
	size_t first = 0;
	while (first < trimmed.size() && isspace((unsigned char)trimmed[first]))
		first++;
	trimmed = trimmed.substr(first);
	if (trimmed.empty())
		return false;
	if (Upper(trimmed).find("FREQ=") == std::string::npos) {
		if (!ParseOldRule(trimmed, &result))
			return false;
		result.valid = true;
		*rule = result;
		return true;
	}

	for (const std::string& part : Split(trimmed, ';')) {
		const size_t equals = part.find('=');
		if (equals == std::string::npos)
			continue;
		const std::string key = Upper(Squeezed(part.substr(0, equals)));
		const std::string value = Squeezed(part.substr(equals + 1));
		if (key == "FREQ") {
			const std::string name = Upper(value);
			if (name == "SECONDLY") result.frequency = Frequency::Secondly;
			else if (name == "MINUTELY") result.frequency = Frequency::Minutely;
			else if (name == "HOURLY") result.frequency = Frequency::Hourly;
			else if (name == "DAILY") result.frequency = Frequency::Daily;
			else if (name == "WEEKLY") result.frequency = Frequency::Weekly;
			else if (name == "MONTHLY") result.frequency = Frequency::Monthly;
			else if (name == "YEARLY") result.frequency = Frequency::Yearly;
		} else if (key == "INTERVAL") {
			result.interval = std::max(1, atoi(value.c_str()));
		} else if (key == "COUNT") {
			result.count = std::max(0, atoi(value.c_str()));
		} else if (key == "UNTIL") {
			ParseDateTime(value, "", false, &result.until);
		} else if (key == "BYSECOND") {
			result.bySecond = Numbers(value);
		} else if (key == "BYMINUTE") {
			result.byMinute = Numbers(value);
		} else if (key == "BYHOUR") {
			result.byHour = Numbers(value);
		} else if (key == "BYMONTHDAY") {
			result.byMonthDay = Numbers(value);
		} else if (key == "BYYEARDAY") {
			result.byYearDay = Numbers(value);
		} else if (key == "BYWEEKNO") {
			result.byWeekNo = Numbers(value);
		} else if (key == "BYMONTH") {
			result.byMonth = Numbers(value);
		} else if (key == "BYSETPOS") {
			result.bySetPos = Numbers(value);
		} else if (key == "BYDAY") {
			for (const std::string& each : Split(Upper(value), ',')) {
				WeekDay day;
				if (ParseWeekDay(each, &day))
					result.byDay.push_back(day);
			}
		} else if (key == "WKST") {
			const int weekday = WeekdayOf(value);
			if (weekday >= 0)
				result.weekStart = weekday;
		}
	}
	if (result.frequency == Frequency::None)
		return false;
	result.valid = true;
	*rule = result;
	return true;
}


namespace {

bool
Contains(const std::vector<int>& list, int value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

/** a number that may count from the end (-1 is the last) matches the value in a range of @p total */
bool
MatchesCounted(const std::vector<int>& list, int value, int total)
{
	for (int each : list) {
		if (each == value || (each < 0 && total + each + 1 == value))
			return true;
	}
	return false;
}

int
YearDay(int year, int month, int day)
{
	return (int)(DaysFromCivil(year, month, day) - DaysFromCivil(year, 1, 1)) + 1;
}

/** the number of the week of the year, the first week being the first with four days in the year (ISO 8601 when the week starts on Monday) */
int
WeekNumber(int year, int month, int day, int weekStart, int* weekYear)
{
	const int64_t days = DaysFromCivil(year, month, day);
	auto weekBegin = [weekStart](int64_t d) {
		const int dow = (int)(((d % 7) + 7 + 4) % 7);
		return d - ((dow - weekStart + 7) % 7);
	};
	// the first week of a year: the one that has its fourth day (Jan 4 counts for a week of Monday) in the year
	auto firstWeek = [&](int y) {
		const int64_t jan1 = DaysFromCivil(y, 1, 1);
		int64_t begin = weekBegin(jan1);
		if (jan1 - begin > 3)
			begin += 7;		// (the week has fewer than four days in the year: the next one is the first)
		return begin;
	};
	int y = year;
	if (days >= firstWeek(year + 1))
		y = year + 1;
	else if (days < firstWeek(year))
		y = year - 1;
	*weekYear = y;
	return (int)((days - firstWeek(y)) / 7) + 1;
}

int
WeeksInYear(int year, int weekStart)
{
	int weekYear = 0;
	// (the last days of the year are in the last week, or in the first of the next year: look at the 28th)
	return WeekNumber(year, 12, 28, weekStart, &weekYear);
}

/** the place of the day among the days of its week day in the month or in the year, from the start (1, 2...) and from the end (-1, -2...) */
bool
OrdinalMatches(int ordinal, int year, int month, int day, bool inMonth)
{
	const int index = inMonth ? day : YearDay(year, month, day);
	const int total = inMonth ? DaysInMonth(year, month) : (IsLeapYear(year) ? 366 : 365);
	if (ordinal > 0)
		return (index - 1) / 7 + 1 == ordinal;
	return (total - index) / 7 + 1 == -ordinal;
}

struct Context {
	const Rule&		rule;
	const DateTime&	start;
};

bool
DayAllowed(const Context& c, int year, int month, int day)
{
	const Rule& r = c.rule;
	const Frequency f = r.frequency;
	if (!r.byMonth.empty() && !Contains(r.byMonth, month))
		return false;
	if (!r.byWeekNo.empty()) {
		int weekYear = 0;
		const int number = WeekNumber(year, month, day, r.weekStart, &weekYear);
		if (weekYear != year && f == Frequency::Yearly)
			return false;
		if (!MatchesCounted(r.byWeekNo, number, WeeksInYear(weekYear, r.weekStart)))
			return false;
	}
	if (!r.byYearDay.empty() && !MatchesCounted(r.byYearDay, YearDay(year, month, day), IsLeapYear(year) ? 366 : 365))
		return false;
	if (!r.byMonthDay.empty() && !MatchesCounted(r.byMonthDay, day, DaysInMonth(year, month)))
		return false;
	const int weekday = DayOfWeek(year, month, day);
	if (!r.byDay.empty()) {
		bool any = false;
		const bool inMonth = f == Frequency::Monthly || (f == Frequency::Yearly && !r.byMonth.empty());
		for (const WeekDay& each : r.byDay) {
			if (each.weekday != weekday)
				continue;
			if (each.ordinal == 0 || (f != Frequency::Monthly && f != Frequency::Yearly) || OrdinalMatches(each.ordinal, year, month, day, inMonth)) {
				any = true;
				break;
			}
		}
		if (!any)
			return false;
	}
	// what the rule leaves open comes from the start
	switch (f) {
		case Frequency::Yearly:
			if (r.byMonth.empty() && r.byWeekNo.empty() && r.byYearDay.empty() && r.byMonthDay.empty() && r.byDay.empty())
				return month == c.start.month && day == c.start.day;
			if (!r.byMonth.empty() && r.byMonthDay.empty() && r.byDay.empty() && r.byYearDay.empty() && r.byWeekNo.empty())
				return day == c.start.day;
			if (!r.byWeekNo.empty() && r.byDay.empty() && r.byYearDay.empty() && r.byMonthDay.empty())
				return weekday == DayOfWeek(c.start.year, c.start.month, c.start.day);
			break;
		case Frequency::Monthly:
			if (r.byMonthDay.empty() && r.byDay.empty())
				return day == c.start.day;
			break;
		case Frequency::Weekly:
			if (r.byDay.empty())
				return weekday == DayOfWeek(c.start.year, c.start.month, c.start.day);
			break;
		default:
			break;
	}
	return true;
}

/** the times of a day: the hours, minutes and seconds of the rule, or those of the start */
std::vector<int>
TimesOfDay(const Rule& rule, const DateTime& start)
{
	std::vector<int> hours = rule.byHour.empty() ? std::vector<int>{start.hour} : rule.byHour;
	std::vector<int> minutes = rule.byMinute.empty() ? std::vector<int>{start.minute} : rule.byMinute;
	std::vector<int> seconds = rule.bySecond.empty() ? std::vector<int>{start.second} : rule.bySecond;
	std::vector<int> times;
	for (int h : hours) {
		for (int m : minutes) {
			for (int s : seconds) {
				if (h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 61)
					times.push_back(h * 3600 + m * 60 + s);
			}
		}
	}
	std::sort(times.begin(), times.end());
	times.erase(std::unique(times.begin(), times.end()), times.end());
	return times;
}

}	// namespace


void
Expand(const Rule& rule, const DateTime& start, const std::function<bool(const DateTime&)>& visit, int limit)
{
	if (!start.valid)
		return;
	const int64_t startSeconds = LocalSeconds(start);
	if (!rule.valid || rule.frequency == Frequency::None) {
		visit(start);
		return;
	}
	int64_t untilSeconds = 0;
	const bool hasUntil = rule.until.valid;
	if (hasUntil)
		untilSeconds = LocalSeconds(rule.until) + (rule.until.dateOnly ? 86399 : 0);

	int visited = 0, counted = 0;
	auto emit = [&](int64_t seconds) {
		// false: stop
		if (seconds < startSeconds)
			return true;
		if (hasUntil && seconds > untilSeconds)
			return false;
		DateTime time = FromLocalSeconds(seconds);
		time.dateOnly = start.dateOnly;
		time.utc = start.utc;
		time.tzid = start.tzid;
		counted++;
		visited++;
		if (!visit(time))
			return false;
		if (rule.count > 0 && counted >= rule.count)
			return false;
		return visited < limit;
	};

	const Context context{rule, start};
	const Frequency f = rule.frequency;

	// ---- more often than a day: the steps of the interval, as far as the rule lets them through
	if (f == Frequency::Hourly || f == Frequency::Minutely || f == Frequency::Secondly) {
		const int64_t unit = f == Frequency::Hourly ? 3600 : f == Frequency::Minutely ? 60 : 1;
		const int64_t step = unit * rule.interval;
		int64_t t = startSeconds;
		for (int64_t steps = 0; steps < 2000000; steps++, t += step) {
			const DateTime time = FromLocalSeconds(t);
			if (hasUntil && t > untilSeconds)
				return;
			if (!rule.byMonth.empty() && !Contains(rule.byMonth, time.month))
				continue;
			if (!rule.byMonthDay.empty() && !MatchesCounted(rule.byMonthDay, time.day, DaysInMonth(time.year, time.month)))
				continue;
			if (!rule.byYearDay.empty() && !MatchesCounted(rule.byYearDay, YearDay(time.year, time.month, time.day), IsLeapYear(time.year) ? 366 : 365))
				continue;
			if (!rule.byDay.empty()) {
				bool any = false;
				for (const WeekDay& day : rule.byDay)
					any = any || day.weekday == DayOfWeek(time.year, time.month, time.day);
				if (!any)
					continue;
			}
			if (!rule.byHour.empty() && !Contains(rule.byHour, time.hour))
				continue;
			if (!rule.byMinute.empty() && !Contains(rule.byMinute, time.minute))
				continue;
			if (!rule.bySecond.empty() && !Contains(rule.bySecond, time.second))
				continue;
			if (!emit(t))
				return;
		}
		return;
	}

	// ---- a day or longer: a period at a time (a day, a week, a month, a year), its days filtered, each with the times of the day
	const std::vector<int> times = TimesOfDay(rule, start);
	if (times.empty())
		return;
	int year = start.year, month = start.month;
	int64_t periodDays = DaysFromCivil(start.year, start.month, start.day);		// (the first day of the period, for a day or a week)
	if (f == Frequency::Weekly) {
		const int dow = DayOfWeek(start.year, start.month, start.day);
		periodDays -= (dow - rule.weekStart + 7) % 7;
	}

	int emptyPeriods = 0;
	for (int64_t period = 0; period < 1000000; period++) {
		// the days of the period
		std::vector<int64_t> days;
		switch (f) {
			case Frequency::Yearly:
				for (int m = 1; m <= 12; m++) {
					for (int d = 1; d <= DaysInMonth(year, m); d++) {
						if (DayAllowed(context, year, m, d))
							days.push_back(DaysFromCivil(year, m, d));
					}
				}
				break;
			case Frequency::Monthly:
				for (int d = 1; d <= DaysInMonth(year, month); d++) {
					if (DayAllowed(context, year, month, d))
						days.push_back(DaysFromCivil(year, month, d));
				}
				break;
			case Frequency::Weekly:
				for (int i = 0; i < 7; i++) {
					int y, m, d;
					CivilFromDays(periodDays + i, &y, &m, &d);
					if (DayAllowed(context, y, m, d))
						days.push_back(periodDays + i);
				}
				break;
			default: {
				int y, m, d;
				CivilFromDays(periodDays, &y, &m, &d);
				if (DayAllowed(context, y, m, d))
					days.push_back(periodDays);
				break;
			}
		}
		// the moments of the period
		std::vector<int64_t> moments;
		for (int64_t day : days) {
			for (int time : times)
				moments.push_back(day * 86400 + time);
		}
		std::sort(moments.begin(), moments.end());
		if (!rule.bySetPos.empty()) {
			std::vector<int64_t> chosen;
			for (int position : rule.bySetPos) {
				const int index = position > 0 ? position - 1 : (int)moments.size() + position;
				if (index >= 0 && index < (int)moments.size())
					chosen.push_back(moments[index]);
			}
			std::sort(chosen.begin(), chosen.end());
			chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
			moments = chosen;
		}
		bool any = false;
		for (int64_t moment : moments) {
			if (moment < startSeconds)
				continue;
			any = true;
			if (!emit(moment))
				return;
		}
		emptyPeriods = any ? 0 : emptyPeriods + 1;
		if (emptyPeriods > 4000)
			return;		// (a rule that never matches: 30 February)

		// the next period
		switch (f) {
			case Frequency::Yearly:
				year += rule.interval;
				break;
			case Frequency::Monthly: {
				int months = month - 1 + rule.interval;
				year += months / 12;
				month = months % 12 + 1;
				break;
			}
			case Frequency::Weekly:
				periodDays += 7 * rule.interval;
				break;
			default:
				periodDays += rule.interval;
				break;
		}
		if (year > 9999)
			return;
	}
}

}	// namespace ics
