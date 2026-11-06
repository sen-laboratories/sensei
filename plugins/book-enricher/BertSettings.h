/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

namespace bert {

/**
 * What BERT takes from the settings of SEN that all plugins share (sen::plugins::LoadSharedSettings(): ask which one if several books are found, the most authors that
 * are made, where the people go). BERT has no settings of its own, so it is not listed in the settings window (`SEN:plugin:hasSettings` is false). The option of the command
 * line (`--auto`) wins over the setting for one run.
 */
struct Settings {
	/** if OpenLibrary finds several books: ask which one is yours; off: the best one is taken, as `--auto` does */
	bool choose = true;
	/** the most authors that are made, for books with a long list */
	int maxAuthors = 10;
};

}	// namespace bert
