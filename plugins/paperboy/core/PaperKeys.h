/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file PaperKeys.h
 * @brief The names that paperboy writes: attributes of the file, fields of the messages inside them, the semantic type.
 *
 * The attributes follow the vocabularies that exist (Dublin Core, schema.org) and are the same for every format, so a column or a
 * query works for a PDF as for an EPUB. What has no established name is `SEN:`; the state of the plugin is one message attribute.
 */
namespace paperboy {

/** The semantic type that the identify feature sets (the attribute META:TYPE, see sen::attr::kType). */
inline constexpr char kSemanticType[] = "org.sen-labs.paper";

namespace attr {
inline constexpr char kIdentifier[]    = "dc:identifier";		///< the DOI, bare: 10.1038/nature14539
inline constexpr char kTitle[]         = "dc:title";
/** the separator of the values of the attributes that are lists (the ontology of papers says is_list for them), see sento SenList.h: a name with a comma is in quotes */
inline constexpr char kListSeparator   = ',';
inline constexpr char kCreator[]       = "dc:creator";			///< the names of the authors, a list
inline constexpr char kSubject[]       = "dc:subject";			///< the keywords, a list
inline constexpr char kDescription[]   = "dc:description";		///< the abstract
inline constexpr char kPublisher[]     = "dc:publisher";
inline constexpr char kLanguage[]      = "dc:language";
inline constexpr char kDate[]          = "dc:date";				///< B_TIME_TYPE, the day of publication
inline constexpr char kType[]          = "dc:type";				///< the kind of work: article, preprint, ...
inline constexpr char kIsPartOf[]      = "dcterms:isPartOf";	///< the journal, conference or repository
inline constexpr char kAuthor[]        = "schema:author";		///< message: one schema::author entry per author
inline constexpr char kEncoding[]      = "schema:encoding";		///< message: one schema::MediaObject entry per place to read the paper
inline constexpr char kUrl[]           = "schema:url";			///< the landing page of the best open access copy, else the DOI
inline constexpr char kContentUrl[]    = "schema:contentUrl";	///< the direct link to the PDF of the best open access copy
inline constexpr char kOaStatus[]      = "SEN:oaStatus";		///< gold, green, hybrid, bronze, diamond or closed
inline constexpr char kCitationCount[] = "SEN:citationCount";	///< B_INT32_TYPE, how often others cite the paper
inline constexpr char kState[]         = "paperboy:state";		///< message: what the plugin found out (see the key namespace)
}

/** Fields of the messages in the attributes. */
namespace key {
// schema:author
inline constexpr char kPerson[]        = "schema:author";
inline constexpr char kName[]          = "schema:name";
inline constexpr char kOrcid[]         = "schema:identifier";	///< https://orcid.org/...
inline constexpr char kAffiliation[]   = "schema:affiliation";
// schema:encoding
inline constexpr char kMediaObject[]   = "schema:MediaObject";
inline constexpr char kContentUrl[]    = "schema:contentUrl";
inline constexpr char kFormat[]        = "schema:encodingFormat";
inline constexpr char kLicense[]       = "schema:license";
inline constexpr char kSource[]        = "SEN:source";			///< unpaywall or openalex
inline constexpr char kOaStatus[]      = "SEN:oaStatus";
inline constexpr char kVersion[]       = "SEN:version";			///< publishedVersion, acceptedVersion, submittedVersion
inline constexpr char kIsBest[]        = "SEN:isBest";
// paperboy:state
inline constexpr char kStatus[]        = "status";				///< ok, doi_not_found, not_in_openalex or title_from_document
inline constexpr char kChecked[]       = "checked";				///< B_TIME_TYPE
inline constexpr char kDoiSource[]     = "doiSource";			///< attribute, metadata, text, arxiv or title
inline constexpr char kOpenAlexId[]    = "openAlexId";
inline constexpr char kUnpaywall[]     = "unpaywall";			///< bool, Unpaywall was asked
inline constexpr char kReference[]     = "reference";			///< message, one per cited work: id, doi, title, year
inline constexpr char kRefId[]         = "id";
inline constexpr char kRefDoi[]        = "doi";
inline constexpr char kRefTitle[]      = "title";
inline constexpr char kRefYear[]       = "year";
}

/** Values of key::kStatus. */
namespace outcome {
inline constexpr char kOk[]            = "ok";
inline constexpr char kDoiNotFound[]   = "doi_not_found";
inline constexpr char kNotInOpenAlex[] = "not_in_openalex";
inline constexpr char kTitleFromDocument[] = "title_from_document";	///< not found online; the title is the one of the document
}

/** Where the DOI came from, in the order of how much it is trusted (values of key::kDoiSource). */
namespace source {
inline constexpr char kAttribute[]     = "attribute";			///< dc:identifier of the file, written earlier
inline constexpr char kMetadata[]      = "metadata";			///< a field of the document (Subject, doi, ...)
inline constexpr char kFirstPage[]     = "text";				///< the text of the first page
inline constexpr char kArxiv[]         = "arxiv";				///< the arXiv stamp of the first page
inline constexpr char kLaterPage[]     = "later";				///< the text of the pages after the first: a cited paper is possible
inline constexpr char kTitle[]         = "title";				///< found by the title in OpenAlex
}

/** Fields of an item in the reply of an extraction, beyond the protocol fields of Sensei.h. */
namespace itemfield {
inline constexpr char kPath[]          = "path";				///< mapped to sen::attr::kToPath
inline constexpr char kDoi[]           = "doi";					///< mapped to dc:identifier
inline constexpr char kTitle[]         = "title";				///< mapped to dc:title
inline constexpr char kYear[]          = "year";				///< mapped to SEN:REL:year
}

}	// namespace paperboy
