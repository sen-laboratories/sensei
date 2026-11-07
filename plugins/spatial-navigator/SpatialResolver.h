/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Message.h>

#include <map>
#include <string>
#include <vector>

#include "../../common/HttpCache.h"
#include "SpatialOverpass.h"
#include "SpatialPlace.h"
#include "SpatialRelations.h"
#include "SpatialSettings.h"
#include "SpatialWikidata.h"

namespace spatial {

/** A place that the place of the file is related to. */
struct Target {
	entry_ref	ref;
	Relation	relation = Relation::kNone;		///< the relation of the place of the file to this one (within: the file's place is in it)
	bool		fromWikidata = false;			///< found in the chain "located in" of Wikidata, not from the outlines
	bool		pointOfInterest = false;		///< a named thing of OpenStreetMap in the area of the place (Overpass), the relation type is the point of interest
};

/**
 * Works out the relations of the place of a file when they are asked for (the relations are stored nowhere, so they are right at that moment, also after a boundary was corrected):
 *
 * - from the places and locations that exist (the files of those types): a place whose outline or position is related to that of the file in one of the ways of the Region Connection
 *   Calculus (the relation of A to B is the DE-9IM matrix of their geometries, worked out with GEOS and mapped, see SpatialRelations.h). Only the places whose bounding boxes meet are asked.
 * - for `within`, from Wikidata: the places that contain the place ("located in", all of the chain: Stephansdom is in Innere Stadt, Vienna and Austria) from its Wikidata link, one request.
 *   A place of that chain that exists as a file (by its Wikidata link) is a target; one that does not is made in the Library (name, link, position, short description) so that it can be opened.
 *   The files that this makes can be enriched with SUESS like any place.
 * - for `point-of-interest`, from OpenStreetMap (Overpass API): the named things in the area of the place (its box, at least 500 m across, at most 3 km), the nearest 100. A thing that exists
 *   as a file (by its OpenStreetMap link) is a target; one that does not is made in the Library with its name, link, position, kind and address. Those are not enriched: there can be a hundred.
 */
class Resolver {
public:
	/** @p language is the language of the names of places that are made (`de`, `en`); @p debug prints what is done; @p libraries are the Library folders that the server gave in its request (sensei::key::kLibraries). */
	Resolver(const std::string& language, bool debug, const BMessage* libraries = NULL);
	~Resolver();

	/** The places that this Resolver made in the Library (the parents that were missing): they have a name, a link, a position and a type, and no picture, outline or address. */
	const std::vector<entry_ref>&	Made() const { return fMade; }

	/** The places that the place of @p file is related to by the relation type (`relation/x-vnd.sen-labs.relation.spatial.within`...). A file with no position, outline or link has none: B_OK. */
	/** how many points of interest this Resolver made (not in Made(): those are not enriched) */
	int MadePointsOfInterest() const { return fMadePois; }

	status_t Resolve(const entry_ref& file, const std::string& relationType, std::vector<Target>* targets);

private:
	/** the parents of the place from Wikidata, as targets; the files that are not there yet are made */
	void				_AddAncestors(const PlaceData& place, const std::map<std::string, entry_ref>& byWikidata, std::vector<Target>* targets);
	/** the places that contain the one with this Wikidata item (kept for the Resolver, and for a month on disk); false if Wikidata could not be asked */
	bool				_Ancestors(const std::string& wikidataId, std::vector<Ancestor>* ancestors);
	/** whether Wikidata says that the place A is located in B or B in A */
	bool				_WikidataContains(const std::string& a, const std::string& b);
	status_t			_MakePlace(const entry_ref& beside, const Ancestor& ancestor, entry_ref* made);
	/** the named things in the area of the place, nearest first (at most kMaxPois), as targets; the files that are not there yet are made */
	void				_AddPointsOfInterest(const PlaceData& place, const std::map<std::string, entry_ref>& byOsm, std::vector<Target>* targets);
	/** the named things in the box (kept for a day on disk); false if Overpass could not be asked */
	bool				_Pois(const Box& area, std::vector<Poi>* pois);
	status_t			_MakePoi(const entry_ref& beside, const Poi& poi, entry_ref* made);
	/** the matrix of two geometries (empty if they cannot be compared) */
	std::string			_Matrix(const std::string& wktA, const std::string& wktB);

	std::string			fLanguage;
	std::map<std::string, std::vector<Ancestor>>	fAncestors;	///< what was asked of Wikidata by this Resolver, by item
	std::vector<entry_ref> fMade;
	int					fMadePois = 0;
	Settings			fSettings;		///< which server answers for the points of interest (SpatialSettingsForm.h)
	bool				fHasLibrary = false;
	entry_ref			fLibrary;			///< the folder for places that the server gave (kLibraries), if it did
	bool				fDebug;
	sensei::DiskCache*	fCache;
	void*				fContext;		///< the context of GEOS (GEOSContextHandle_t)
};

}	// namespace spatial
