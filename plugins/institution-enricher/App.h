/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <Entry.h>
#include <String.h>

#include <string>

#include "HttpFetcher.h"
#include "core/InstitutionApi.h"

namespace campus {

/**
 * SEN Campus, the enricher of institutions: looks an institution up in the Research Organization Registry (ROR) and OpenAlex and writes what it finds to the file.
 * Started by the SEN server for a file of the type Institution (a message of refs), or from the command line.
 */
class App : public BApplication {
public:
	App();

	void AboutRequested();
	void RefsReceived(BMessage* message);
	void ArgvReceived(int32 argc, char** argv);

	int ExitCode() const { return fExitCode; }

private:
	status_t Enrich(BMessage* message, BMessage* reply);
	status_t EnrichOne(const entry_ref& file, bool wipe, bool automatic, bool relations, int units, BString* summary);

	/** the institution that the file is: by its id, else by the best answer to its name (the user chooses between several, unless @p automatic) */
	status_t Find(const std::string& name, const std::string& rorId, bool automatic, institution::Record* record, BString* why);
	/** what OpenAlex adds; false if it does not answer (the registry's answer is enough) */
	bool AddOpenAlex(institution::Record* record, BString* why);

	void LoadAccess();

	HttpFetcher					fHttp;
	institution::OpenAlexAccess	fAccess;
	bool						fDebug;
	int							fExitCode;
};

}	// namespace campus
