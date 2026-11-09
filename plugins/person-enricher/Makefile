# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.

## Haiku Generic Makefile v2.6 ##

NAME = SenPersonEnricher
TARGET_DIR = bin
TYPE = APP

APP_MIME_SIG = application/x-vnd.sen-plugin.sen-labs.person-enricher

SRCS = App.cpp PersonWriter.cpp HttpFetcher.cpp StatusStore.cpp core/PersonEnricher.cpp core/GravatarResolver.cpp core/GitHubResolver.cpp core/FeedResolver.cpp core/FeedText.cpp \
	../../common/HttpCache.cpp ../../common/ThumbnailUtil.cpp
RDEFS = Resources.rdef
RSRCS =

LIBS = be bnetapi netservices2 shared translation $(STDCPPLIBS)
LIBPATHS =

SYSTEM_INCLUDE_PATHS = $(shell findpaths -e B_FIND_PATH_HEADERS_DIRECTORY) \
	$(HOME)/config/non-packaged/develop/headers \
	/boot/system/develop/headers/private/netservices \
	/boot/system/develop/headers/private/netservices2 \
	/boot/system/develop/headers/private/shared

LOCAL_INCLUDE_PATHS = core

OPTIMIZE :=
LOCALES =
DEFINES =
WARNINGS =
SYMBOLS :=
DEBUGGER := TRUE
COMPILER_FLAGS = -std=gnu++20 -fPIC
LINKER_FLAGS =

DRIVER_PATH =

DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
