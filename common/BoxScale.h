/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file BoxScale.h
 * @brief Makes an image smaller (header only, plain C++: no Haiku, tested on every platform).
 *
 * A photo that a service gives in its own size (a profile photo of 1000 pixels) is a thumbnail of a few hundred at most: kept as it is, it is an attribute of 100 KB or more on every person.
 * The image is made smaller by averaging the pixels of the original that a pixel of the result covers (a box filter: no jagged edges, nothing lost to a skipped pixel), the aspect is kept.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sensei {

/** The size that an image of `width` x `height` gets when its longer side may be `maxSide` pixels: the same if it is not longer, else scaled with the aspect kept (at least 1 pixel). */
inline void
ScaledSize(int width, int height, int maxSide, int* newWidth, int* newHeight)
{
	*newWidth = width;
	*newHeight = height;
	if (maxSide <= 0 || width <= 0 || height <= 0 || std::max(width, height) <= maxSide)
		return;
	if (width >= height) {
		*newWidth = maxSide;
		*newHeight = std::max(1, (int)std::lround((double)height * maxSide / width));
	} else {
		*newHeight = maxSide;
		*newWidth = std::max(1, (int)std::lround((double)width * maxSide / height));
	}
}


/**
 * Scales pixels of four bytes each (any order of the channels: all are treated alike, so RGBA and BGRA both work) from `width` x `height` to `newWidth` x `newHeight` (not larger than the
 * original). Rows are `width * 4` bytes, without padding.
 */
inline std::vector<uint8_t>
BoxScale(const std::vector<uint8_t>& source, int width, int height, int newWidth, int newHeight)
{
	std::vector<uint8_t> result((size_t)newWidth * newHeight * 4);
	for (int y = 0; y < newHeight; y++) {
		const double top = (double)y * height / newHeight;
		const double bottom = (double)(y + 1) * height / newHeight;
		for (int x = 0; x < newWidth; x++) {
			const double left = (double)x * width / newWidth;
			const double right = (double)(x + 1) * width / newWidth;
			double sum[4] = {0, 0, 0, 0};
			double weight = 0;
			for (int sy = (int)top; sy < (int)std::ceil(bottom) && sy < height; sy++) {
				const double covered_y = std::min((double)sy + 1, bottom) - std::max((double)sy, top);
				for (int sx = (int)left; sx < (int)std::ceil(right) && sx < width; sx++) {
					const double covered = covered_y * (std::min((double)sx + 1, right) - std::max((double)sx, left));
					const uint8_t* pixel = &source[((size_t)sy * width + sx) * 4];
					for (int c = 0; c < 4; c++)
						sum[c] += pixel[c] * covered;
					weight += covered;
				}
			}
			uint8_t* out = &result[((size_t)y * newWidth + x) * 4];
			for (int c = 0; c < 4; c++)
				out[c] = weight > 0 ? (uint8_t)std::lround(std::min(255.0, sum[c] / weight)) : 0;
		}
	}
	return result;
}

}	// namespace sensei
