/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "ThumbnailUtil.h"

#include <Node.h>
#include <TypeConstants.h>

#include <time.h>

status_t
WriteThumbnail(const entry_ref* ref, const std::string& image)
{
    BNode node(ref);
    status_t result = node.InitCheck();
    if (result != B_OK)
        return result;

    // The creation time of the thumbnail has to be after the change time of the file, or Tracker removes it. It is written FIRST: Tracker updates the icon of a file when the
    // attribute of the thumbnail changes, and finds no thumbnail (no valid time) if the time comes after it; the time is no icon attribute, so the icon would stay as it is.
    // (Writing attributes does not change the modification time of the file.)
    time_t modtime;
    result = node.GetModificationTime(&modtime);
    if (result != B_OK)
        return result;
    modtime++;

    ssize_t written = node.WriteAttr(THUMBNAIL_CREATION_TIME, B_TIME_TYPE, 0, &modtime, sizeof(time_t));
    if (written < 0)
        return (status_t) written;
    if ((size_t) written < sizeof(time_t))
        return B_IO_ERROR;

    written = node.WriteAttr(THUMBNAIL_ATTR_NAME, B_RAW_TYPE, 0, image.data(), image.length());
    if (written < 0)
        return (status_t) written;
    if ((size_t) written < image.length())
        return B_IO_ERROR;

    node.Sync();
    return B_OK;
}
