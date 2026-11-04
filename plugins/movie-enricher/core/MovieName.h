/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

namespace movie {

/** What the name of a file says about the movie. */
struct Guess {
	std::string title;
	int year = 0;		///< 0: none
};

/**
 * The title and the year that a file name stands for: the name of a video file is what a person or a ripping program made of it
 * (`The.Matrix.1999.1080p.BluRay.x264-YIFY.mkv`, `Blade Runner 2049 (2017).mp4`). The extension of a video file goes, dots and underscores are blanks, the year (in
 * brackets, or after the title) ends the title, and so does the first word of how the file was made (1080p, BluRay, x264, ...). A number at the start of the name is part of the title
 * (`1917.2019.mkv` is 1917, from 2019). Plain C++.
 */
Guess GuessFromFileName(const std::string& fileName);

}	// namespace movie
