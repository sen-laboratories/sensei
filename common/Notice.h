/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file Notice.h
 * @brief A message of a plugin to the user that does not block it (header only).
 *
 * A plugin that has done its work, or could not, says so with a notification of Haiku (the Notification server, shown in the corner of the screen and gone by itself) and does
 * not wait for a click on a dialog: the plugin quits, scripts and the server are not held up, and nothing stays on the screen that somebody has to close. Use it for the result
 * of an enrichment: what was done (a click on the notification opens the file) or why not.
 *
 * It also says that the work has begun (NotifyStarted()), as a lookup over the network can take a while and the user would not know that anything happens. The notification of the
 * result has the same message id as that of the start, so that it takes its place: one notification for the file at a time, not two.
 *
 *     sensei::NotifyStarted("SEN Book Enricher", "Enriching book", "The BeOS Bible");
 *     sensei::Notify("SEN Book Enricher", "Book enriched", "The BeOS Bible", true, &ref);
 *     sensei::Notify("SEN Book Enricher", "Book not enriched", "OpenLibrary has no book called ...", false);
 */

#include <Entry.h>
#include <Notification.h>
#include <String.h>

namespace sensei {

/**
 * @param group the name of the plugin: notifications of a group replace one another instead of piling up
 * @param success an information (true) or an error (false)
 * @param openOnClick the file that a click on the notification opens, NULL for none
 */
inline void
Notify(const char* group, const char* title, const char* text, bool success, const entry_ref* openOnClick = NULL)
{
	BNotification notification(success ? B_INFORMATION_NOTIFICATION : B_ERROR_NOTIFICATION);
	notification.SetGroup(group);
	notification.SetMessageID(BString(group) << ":work");		// (replaces the notification of the start)
	notification.SetTitle(title);
	notification.SetContent(text);
	if (success && openOnClick != NULL)
		notification.SetOnClickFile(openOnClick);
	notification.Send();
}


/**
 * Says that the plugin has begun with a file (looking it up, extracting from it). The result, Notify() of the same @p group, replaces it. Call it where the work really starts,
 * after the checks that fail at once (no key, no file): those say why with the notification of the error, and a notification of the start would only flash before it.
 *
 * @param group the name of the plugin, as for Notify()
 * @param title what is done ("Enriching movie")
 * @param text what it is done with (the name of the file)
 */
inline void
NotifyStarted(const char* group, const char* title, const char* text)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(group);
	notification.SetMessageID(BString(group) << ":work");
	notification.SetTitle(title);
	notification.SetContent(text);
	notification.Send();
}

}	// namespace sensei
