/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <SupportDefs.h>

#include <spdlog/spdlog.h>

/**
 * Asks the SEN server and waits for the answer. The server answers in a few milliseconds, but now and then it is busy for longer (a query for the identity of a new file runs over the
 * volumes): a request that is not answered in time is asked again once, with the same message: what Paperboy asks (a relation that is there already is answered as done) can be asked twice.
 * Without this, an author or an institution is left out of the result with a warning in the log, and the user sees a paper that is half enriched.
 */
inline status_t
AskServer(BMessenger& server, BMessage* request, BMessage* answer)
{
	status_t result = B_ERROR;
	for (int attempt = 1; attempt <= 2; attempt++) {
		result = server.SendMessage(request, answer, 20000000, 20000000);
		if (result != B_TIMED_OUT && result != B_WOULD_BLOCK)
			break;
		if (attempt == 1)
			spdlog::info("the SEN server did not answer in 20 s, asking again");
	}
	return result;
}
