/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
// icon2png <application file> <size> <out.png>: the vector icon of an application (its attribute BEOS:ICON, as the Deskbar and Tracker show it) drawn at <size> pixels with transparency, as a PNG. A plugin that has no
// picture of its own for its About window gets its icon made bigger this way: images/<name>-logo.png (sen/SenAbout.h, package.sh). Run it on Haiku.
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <IconUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: icon2png <application> <size> <out.png>\n");
		return 1;
	}
	BApplication app("application/x-vnd.sen-labs.icon2png");
	const int size = atoi(argv[2]);
	BFile file(argv[1], B_READ_ONLY);
	BAppFileInfo info(&file);
	uint8* data = NULL;
	size_t length = 0;
	if (info.InitCheck() != B_OK || info.GetIcon(&data, &length) != B_OK || data == NULL) {
		fprintf(stderr, "%s has no vector icon\n", argv[1]);
		return 1;
	}
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (BIconUtils::GetVectorIcon(data, length, bitmap) != B_OK) {
		fprintf(stderr, "the icon cannot be drawn\n");
		return 1;
	}
	BFile out(argv[3], B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	BBitmapStream stream(bitmap);
	status_t result = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &out, B_PNG_FORMAT);
	printf("%s: %s\n", argv[3], result == B_OK ? "written" : "failed");
	return result == B_OK ? 0 : 1;
}
