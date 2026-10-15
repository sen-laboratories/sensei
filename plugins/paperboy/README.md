<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN Paperboy

Finds out what a scientific paper is, fetches its metadata and tells you where it can be read for free. For PDF and EPUB files, with the features

| Feature | What it does | Network |
|---|---|---|
| **identify** | A DOI that the document states (a field of the file, the first page, an arXiv stamp) says "paper": sets the semantic type `META:TYPE` = `document/scientific-paper` | no |
| **enrich** | Looks the paper up in [OpenAlex](https://openalex.org) and [Unpaywall](https://unpaywall.org) and writes title, authors with ORCID and affiliation, date, keywords, abstract, venue, the open access status and links, the number of citations and the cited works into the attributes of the file | yes |
| **extract** | The papers that this one cites, as references (outgoing relations); a cited paper that is on this computer is told by its path | only if not enriched |

```sh
SenPaperboy --identify paper.pdf          # is it a paper? (sets the semantic type)
SenPaperboy --enrich --email me@example.org *.pdf    # look up and write the attributes
SenPaperboy --extract --local paper.pdf   # cited papers that are on this computer
```
The server calls the plugin with the file (`B_REFS_RECEIVED` for the extraction, `'SEer'` for enrich, `'SEid'` for identify); `SenPaperboy --help` lists the options.

## How the DOI is found

In this order, the first that OpenAlex knows wins:

1. `dc:identifier` of the file (from an earlier run, or by a person)
2. the fields of the document that hold a DOI: Subject, Keywords, `doi`, `DOI`, `WPS-ARTICLEDOI`, ...
3. the text of the **first page**; an arXiv stamp (`arXiv:1706.03762v5`) gives the DOI that DataCite registers for it, `10.48550/arxiv.1706.03762`
4. a DOI on the following pages (the first 3 pages are read, `max_pages`): only the first two, they are most likely cited papers, and *identify* does not take them as a sign of a paper
5. the **title** (the Title field of the document if it looks like a title, else the biggest text of the first page) searched in OpenAlex; a hit is taken only if its title is at least 85 % the same words

The text comes from MuPDF (PDF and EPUB; a book is laid out first). A file that needs a password is not read.

## What is written

The attributes are the same for all formats and use the vocabularies that exist (Dublin Core, schema.org), `SEN:` where there is none. **Bibliographic data fills what is missing** (what a person or another program has put into a file stays, `--overwrite` or `overwrite = yes` replaces it); **the data of access is refreshed on every run**.

| Attribute | Type | Content (source) |
|---|---|---|
| `dc:identifier` | string | the DOI, bare and lower case: `10.1038/nature14539` (another kind of identifier that is there stays) |
| `dc:title`, `dc:type` | string | title; the kind of work (`article`, `preprint`, `book-chapter`, ...) |
| `dc:creator` | string | the names of the authors, separated by `;` |
| `schema:author` | message | one `schema:author` entry per author: `schema:name`, `schema:identifier` (ORCID URL), `schema:affiliation` |
| `dc:date` | time | day of publication |
| `dc:subject` | string | keywords (OpenAlex `keywords` with a score of 0.3 or more; if there are none the `concepts`, else the `topics`), separated by `;` |
| `dc:description` | string | the abstract, rebuilt from the inverted index of OpenAlex |
| `dc:publisher`, `dc:language`, `dcterms:isPartOf` | string | publisher, language, journal / conference / repository |
| `schema:url` | string | landing page of the best open access copy, else `https://doi.org/<doi>` |
| `schema:contentUrl` | string | direct link to the PDF of the best open access copy (only if there is one) |
| `SEN:oaStatus` | string | `gold`, `green`, `hybrid`, `bronze`, `diamond` or `closed` |
| `schema:encoding` | message | one `schema:MediaObject` per place to read the paper (best first): `schema:contentUrl`, `schema:encodingFormat`, `SEN:source` (`unpaywall` or `openalex`), `SEN:oaStatus`, `SEN:version`, `schema:license`, `SEN:isBest` |
| `SEN:citationCount` | int32 | how often the paper is cited |
| `paperboy:state` | message | what the plugin found out: `status` (`ok`, `doi_not_found`, `not_in_openalex`), `checked`, `doiSource`, `openAlexId`, `unpaywall`, and a `reference` message per cited work (`id`, `doi`, `title`, `year`; up to 500), so that the extraction needs no network |
| `META:TYPE` | string | `document/scientific-paper` (with the Haiku type `BEOS:TYPE` made sure first) |

The attributes live in BFS: they are lost when the file is copied to another file system. `dc:identifier` is indexed on the volume of an enriched file (needed to find cited papers on this computer); packagefs cannot be indexed.

## Citations

Extraction returns one item with an entry for each cited work in each field (the format of the server): `_label` (title, else DOI), `path` (the file on this computer that has the cited DOI as `dc:identifier`, else `https://doi.org/<doi>`), `doi`, `title`, `year`. The type is the generic reference (`relation/x-vnd.sen-labs.relation.reference`), the fields are mapped to `SEN:TO:PATH`, `dc:identifier`, `dc:title` and `SEN:REL:year`. A paper has no contained relations: a request for them (`--self`) is answered with nothing.
The papers that cite this one are not stored (only their number); relations are the business of the server, the plugin writes none.

## Settings

A plugin has no window: `~/config/settings/sen/paperboy.settings`, lines of `key = value` (`#` starts a comment); the command line and the environment (`SEN_PAPERBOY_EMAIL`) win over it.

| Key | Default | |
|---|---|---|
| `email` | - | Your address for the "polite pool": OpenAlex gets it as `mailto` and in the user agent, and **Unpaywall requires it**. Without it only OpenAlex is asked, which has the open access data of Unpaywall, too |
| `unpaywall` | yes | ask Unpaywall for the open access status and the locations |
| `rename` | no | rename the file after `rename_pattern` once it is enriched |
| `rename_pattern` | `{year}_{first_author}_{title}` | tokens `{year}` `{first_author}` `{title}` `{doi}`; `2015_LeCun_Deep_learning.pdf`. A name that exists gets `_2`, `_3`, ... |
| `overwrite` | no | replace title, authors, ... that the file already has |
| `title_search` | yes | look a paper up by its title if there is no DOI |
| `max_pages` | 3 | pages from the start that are searched for a DOI |
| `local_only` | no | extraction: only cited papers that are on this computer |

**Privacy.** What leaves the computer: the DOI, and your address if you gave one. The title search sends the title (at most 250 characters) of the document: switch it off with `title_search = no`. Nothing else of the document is sent. The requests are paced (about 6 a second, a "slow down" is waited out) and sequential: a batch of files is worked one by one.

## Build and test

On Haiku (needs `mupdf1.28_devel`, `spdlog_devel`, `nlohmann_json`):

```sh
./build.sh -j4            # bin/SenPaperboy, with the attributes of the plugin
../../package.sh paperboy # sen_plugin_paperboy-<version>.hpkg
```
`core/` has the logic that needs no Haiku (DOI scanner, readers of the answers, abstract, names, settings; standard C++ and nlohmann/json), tested on any machine with `tests/run.sh` (set `JSON_INCLUDE` to the folder that has `nlohmann/`). `tests/plugin-client.sh` builds a client that talks to the installed plugin like the server does.

| File | |
|---|---|
| `App.cpp` | the application: command line, requests of the server, settings |
| `Paperboy.cpp` | identify, enrich, extract: how the pieces fit |
| `PaperText.cpp` | text and fields of the first pages (MuPDF) |
| `PaperHttp.cpp` | the polite HTTP client |
| `PaperStore.cpp` | reading and writing attributes, finding cited papers on this computer |
| `core/` | everything else |

## Differences from the first specification

[`docs/spec.md`](docs/spec.md) is the specification this started from. It was written without SEN's conventions, so:

* **Attributes, not `xattr` keys:** `sen.schema:name` is `dc:title`, `sen.dcterms:identifier` is `dc:identifier`, `sen.schema:datePublished` is `dc:date` (a time), `sen.schema:keywords` is `dc:subject`, `sen.schema:abstract` is `dc:description`, `sen.pdf_url` is `schema:contentUrl`, `sen.oa_status` is `SEN:oaStatus`, `sen.schema:type` is the semantic type `META:TYPE`, `sen.status` is in `paperboy:state`. Structured values are messages (`schema:author`, `schema:encoding`), not JSON strings.
* **Relations are the server's.** The plugin does not write `SEN:relation`; the extraction returns the citations in the format of the plugins. `dcterms:isReferencedBy` is only the count.
* **Two plugins in one:** the "Enricher" (Unpaywall) and the "Extractor" (OpenAlex) are the features *enrich* and *extract* of one plugin; OpenAlex also supplies the bibliographic data and the open access data when Unpaywall cannot be asked (no address) or does not know the DOI (arXiv).
* **Keywords** come from the OpenAlex `keywords` (the `concepts` are deprecated and only the fallback).
* **DOI search** uses MuPDF (no GROBID or PDFMiner), the fields of the document and the arXiv stamp, and looks first on the first page. The title search uses `search=` (ranked) and checks the hit instead of taking the first.
* **Renaming** is off unless the settings say so.

## Licenses

The source is MIT. The plugin is linked to MuPDF (AGPL), so the package is under the terms of the AGPL, too (`NOTICE-mupdf` in its documentation). The data of OpenAlex and Unpaywall is open (CC0).
