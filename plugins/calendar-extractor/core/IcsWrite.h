/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include "IcsCalendar.h"
#include "IcsParser.h"

/**
 * @file IcsWrite.h
 * @brief Calendar text from what the parser read: one event of a calendar file as a calendar file of its own. Plain C++.
 */

namespace ics {

/** The text of a component and of what is in it: lines that end in CRLF and are folded after 75 bytes (never inside a UTF-8 character), as RFC 5545 has it. */
std::string Serialize(const Component& component);

/**
 * One event of a calendar file as a calendar file of its own: a VCALENDAR with the version, the product and the calendar scale of the file, the zones (VTIMEZONE) that the event refers
 * to and the VEVENTs of the key (EventKey(): the UID; a series with its moved instances is one event: all of them come along). Empty if the file has no event with this key.
 * An event that has no UID gets its key as UID, so that the file of its own is found again by it. @p calendar is what ReadCalendar() made of the same text as @p parsed.
 *
 * The values are as the parser has them, so what was read from another encoding (quoted-printable in a vCalendar 1.0 file) is written as plain text: a line break in it as `\n`, and the
 * parameters ENCODING and CHARSET are left out. The result is read by ReadCalendar() as the event was.
 */
std::string EventFile(const Parsed& parsed, const Calendar& calendar, const std::string& key);

}	// namespace ics
