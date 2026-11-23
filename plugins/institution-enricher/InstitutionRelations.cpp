/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "InstitutionRelations.h"

#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <String.h>

#include <climits>
#include <stdio.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenMessages.h>
#include <sen/SenOntoPeople.h>

#include "InstitutionFile.h"

namespace campus {

namespace {

/** the relation of the type between two files (a service that is not there is not an error: the files are, the relation comes with the next enrichment) */
status_t
Relate(const char* type, const entry_ref& source, const entry_ref& target, const BMessage* properties)
{
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid())
		return B_NOT_INITIALIZED;

	BMessage add(sen::cmd::kRelationAdd), answer;
	add.AddString(sen::key::kRelationType, type);
	add.AddRef(sen::key::kSourceRef, &source);
	add.AddRef(sen::key::kTargetRef, &target);
	if (properties != NULL)
		add.AddMessage(sen::key::kRelationProperties, properties);
	status_t result = server.SendMessage(&add, &answer, 10000000, 10000000);
	if (result != B_OK)
		return result;
	if (answer.GetInt32(sen::key::kStatus, 0) >= 400) {
		printf("the SEN server: %s\n", answer.GetString(sen::key::kDetail, "no relation"));
		return B_ERROR;
	}
	return B_OK;
}

}	// namespace


RelationResult
MakeRelations(const entry_ref& file, const institution::Record& record, int maxUnits, bool debug)
{
	RelationResult result;

	// the larger institution and the units
	int units = 0;
	for (const institution::Related& related : record.related) {
		const bool parent = related.kind == "parent", child = related.kind == "child";
		if (!parent && !child)
			continue;
		if (child && units >= maxUnits)
			continue;
		const std::string name = related.name.empty() ? related.rorId : related.name;

		entry_ref other;
		bool created = false;
		if (FindOrMakeInstitution(file, name, related.rorId, &other, &created) != B_OK) {
			printf("could not make %s, skipping\n", name.c_str());
			continue;
		}
		if (debug)
			printf("%s '%s' (%s)\n", created ? "making" : "found", name.c_str(), related.kind.c_str());
		if (created)
			result.made++;
		if (child)
			units++;

		// (the registry names the other one as the parent: this institution is part of it; a child is a part of this one)
		status_t status = parent ? Relate(sen::onto::people::mime::kPartOfInstitution, file, other, NULL)
			: Relate(sen::onto::people::mime::kPartOfInstitution, other, file, NULL);
		if (status == B_OK)
			result.related++;
		else if (status != B_NOT_INITIALIZED)
			printf("could not relate %s: %s\n", name.c_str(), strerror(status));
	}

	return result;
}

}	// namespace campus
