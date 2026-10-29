/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include <string>

/** The attribute that holds the thumbnail of a file (an encoded image, as Tracker keeps it) and the time that it was made. */
#define THUMBNAIL_ATTR_NAME     "Media:Thumbnail"
#define THUMBNAIL_CREATION_TIME THUMBNAIL_ATTR_NAME ":CreationTime"

/**
 * Writes an image (JPEG, PNG, WebP: what the translators read) as the thumbnail of a file, and checks that all of it was written.
 * The creation time of the thumbnail is set after the change time of the file, or Tracker removes it as out of date.
 * @return B_OK, B_IO_ERROR if only a part could be written, or the error of the file system
 */
status_t WriteThumbnail(const entry_ref* ref, const std::string& image);
