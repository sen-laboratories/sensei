/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

#include <map>
#include <string>
#include <vector>

#include "../../common/HttpCache.h"
#include "SpatialPlace.h"
#include "SpatialRelations.h"
#include "SpatialWikidata.h"

namespace spatial {

/** A place that the place of the file is related to. */
struct Target {
	entry_ref	ref;
	Relation	relation = Relation::kNone;		///< the relation of the place of the file to this one (within: the file's place is in it)
	bool		fromWikidata = false;			///< found in the chain "located in" of Wikidata, not from the outlines
};

/**
 * Works out the relations of the place of a file when they are asked for (the relations are stored nowhere, so they are right at that moment, also after a boundary was corrected):
 *
 * - from the places and locations that exist (the files of those types): a place whose outline or position is related to that of the file in one of the ways of the Region Connection
 *   Calculus (the relation of A to B is the DE-9IM matrix of their geometries, worked out with GEOS and mapped, see SpatialRelations.h). Only the places whose bounding boxes meet are asked.
 * - for `within`, from Wikidata: the places that contain the place ("located in", all of the chain: Stephansdom is in Innere Stadt, Vienna and Austria) from its Wikidata link, one request.
 *   A place of that chain that exists as a file (by its Wikidata link) is a target; one that does not is made in the Library (name, link, position, short description) so that it can be opened.
 *   The files that this makes can be enriched with SUESS like any place.
 */
class Resolver {
public:
	/** @p language is the language of the names of places that are made (`de`, `en`); @p debug prints what is done. */
	Resolver(const std::string& language, bool debug);
	~Resolver();

	/** The places that the place of @p file is related to by the relation type (`relation/x-vnd.sen-labs.relation.spatial.within`...). A file with no position, outline or link has none: B_OK. */
	status_t Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets);

private:
	/** the parents of the place from Wikidata, as targets; the files that are not there yet are made */
	void				_AddAncestors(const PlaceData& place, const std::map<std::string, entry_ref>& byWikidata, std::vector<Target>* targets);
	status_t			_MakePlace(const entry_ref& beside, const Ancestor& ancestor, entry_ref* made);
	/** the matrix of two geometries (empty if they cannot be compared) */
	std::string			_Matrix(const std::string& wktA, const std::string& wktB);

	std::string			fLanguage;
	bool				fDebug;
	sensei::DiskCache*	fCache;
	void*				fContext;		///< the context of GEOS (GEOSContextHandle_t)
};

}	// namespace spatial
