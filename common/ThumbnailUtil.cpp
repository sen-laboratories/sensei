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

    ssize_t written = node.WriteAttr(THUMBNAIL_ATTR_NAME, B_RAW_TYPE, 0, image.data(), image.length());
    if (written < 0)
        return (status_t) written;
    if ((size_t) written < image.length())
        return B_IO_ERROR;

    // the creation time of the thumbnail has to be after the change time of the file, or Tracker removes it
    time_t modtime;
    result = node.GetModificationTime(&modtime);
    if (result != B_OK)
        return result;
    modtime++;

    written = node.WriteAttr(THUMBNAIL_CREATION_TIME, B_TIME_TYPE, 0, &modtime, sizeof(time_t));
    if (written < 0)
        return (status_t) written;
    if ((size_t) written < sizeof(time_t))
        return B_IO_ERROR;

    node.Sync();
    return B_OK;
}
