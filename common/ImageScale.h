/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <SupportDefs.h>

#include <string>

/**
 * Makes an image (JPEG, PNG, WebP, ...: what the translators of Haiku read) smaller: its longer side is at most @p maxSide pixels, the aspect is kept (BoxScale.h). A picture that is small
 * enough is returned as it is, byte for byte. The result is a JPEG if the image has no transparency, else a PNG. Needs the translation kit (link `translation`).
 * @return B_OK, B_BAD_VALUE if the data is no image that a translator reads, or the error of the translators
 */
status_t ScaleImage(const std::string& image, int32 maxSide, std::string* scaled);
