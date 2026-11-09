/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "ImageScale.h"

#include <Bitmap.h>
#include <BitmapStream.h>
#include <DataIO.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>

#include <vector>

#include "BoxScale.h"

status_t
ScaleImage(const std::string& image, int32 maxSide, std::string* scaled)
{
	scaled->clear();
	BMemoryIO input(image.data(), image.size());
	BBitmap* decoded = BTranslationUtils::GetBitmap(&input);
	if (decoded == NULL)
		return B_BAD_VALUE;

	const int width = decoded->Bounds().IntegerWidth() + 1;
	const int height = decoded->Bounds().IntegerHeight() + 1;
	int newWidth = width, newHeight = height;
	sensei::ScaledSize(width, height, (int)maxSide, &newWidth, &newHeight);
	if (newWidth == width && newHeight == height) {
		// (small enough: kept as it is, not decoded and encoded again, which would only lose)
		delete decoded;
		*scaled = image;
		return B_OK;
	}

	// the pixels as 32 bit with the channels in the order of the bitmap; a bitmap of another colour space is converted
	BBitmap bitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
	status_t result = bitmap.InitCheck();
	if (result == B_OK)
		result = bitmap.ImportBits(decoded);
	delete decoded;
	if (result != B_OK)
		return result;

	std::vector<uint8_t> pixels((size_t)width * height * 4);
	const uint8* bits = (const uint8*)bitmap.Bits();
	bool transparent = false;
	for (int y = 0; y < height; y++) {
		const uint8* row = bits + (size_t)y * bitmap.BytesPerRow();
		for (int x = 0; x < width; x++) {
			for (int c = 0; c < 4; c++)
				pixels[((size_t)y * width + x) * 4 + c] = row[x * 4 + c];
			// (B_RGBA32 is stored blue, green, red, alpha)
			if (row[x * 4 + 3] != 255)
				transparent = true;
		}
	}

	const std::vector<uint8_t> small = sensei::BoxScale(pixels, width, height, newWidth, newHeight);
	BBitmap result32(BRect(0, 0, newWidth - 1, newHeight - 1), B_RGBA32);
	if (result32.InitCheck() != B_OK)
		return B_NO_MEMORY;
	uint8* target = (uint8*)result32.Bits();
	for (int y = 0; y < newHeight; y++) {
		for (int x = 0; x < newWidth * 4; x++)
			target[(size_t)y * result32.BytesPerRow() + x] = small[(size_t)y * newWidth * 4 + x];
	}

	BBitmapStream stream(&result32);
	BMallocIO output;
	result = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &output, transparent ? B_PNG_FORMAT : B_JPEG_FORMAT);
	BBitmap* detached = NULL;
	stream.DetachBitmap(&detached);		// (the bitmap is on our stack: the stream must not delete it)
	if (result != B_OK)
		return result;
	scaled->assign((const char*)output.Buffer(), output.BufferLength());
	return scaled->empty() ? B_ERROR : B_OK;
}
