<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN Campus, the institution enricher

*SEN Campus* looks an **institution** up in the [Research Organization Registry](https://ror.org) (ROR) and in [OpenAlex](https://openalex.org) and writes what it finds to the file. Choose **Enrich**
on a file of the type **Institution** in Akita (a university, an institute, a library, a museum: the entity type of the ontology *SEN People*).

> Data: ROR (CC0) and OpenAlex (CC0).

## How the institution is found

1. A file that has a **ROR id** (the attribute *ROR*, `dc:identifier`, bare like `04d836q62`, or a link to `ror.org/...` in its links) is looked up by it: nothing is searched.
2. Else the institution is **searched by name**: its *Full Name*, else the name of the file (an underscore is a blank). The registry scores the answers (its *affiliation* search, made for the
   institution as an author writes it on a paper: "Vienna University of Technology" finds TU Wien) and says which one it takes. That one is taken without asking if it is the only one, if the name is its
   name, or if the others fit worse. Else you **choose** (the best first, with the city and the country); the question can be switched off in the settings of SEN, then (and with `--auto`) the best is
   taken, but only one that fits (the registry's choice, or the name, or a score of 0.9).
3. OpenAlex is asked for the institution **by the id of the registry** (OpenAlex links to ROR, not the other way round): the counts and the topics.

## What is written

What the file has is kept; *Enrich (Overwrite)* (Shift) replaces it. The counts are always the latest.

| | Attribute |
|---|---|
| Name (the one that the registry shows), other names (acronyms first, then aliases and names in other languages, at most 12) | `foaf:name`, `schema:alternateName` |
| ROR id | `dc:identifier` (indexed: a paper that names the institution finds it) |
| Types (a list: a university that funds research is *Education* and *Funder*) | `SEN:institutionType` |
| Web site, city, country (ISO 3166-1) | `foaf:homepage`, `schema:addressLocality`, `schema:addressCountry` |
| Links: ROR, Wikidata, Wikipedia, OpenAlex, ISNI, GRID | `schema:sameAs` |
| The 8 most frequent topics of OpenAlex, the most frequent first | `dc:subject` |
| Works and citations (OpenAlex) | `SEN:worksCount`, `SEN:citationCount` |

A file with no semantic type gets the type **Institution**. The attributes are those of the ontology *SEN People*.

### Relations

The registry knows the larger institution (the parent) and the units of an institution (a faculty, an institute: a large university has dozens). They are **institutions of their own** (a file with the name,
the ROR id and the link, made in the Library of the current context, found again by the id) with the relation **Part Of** (`relation/x-vnd.sen-labs.relation.institution.part-of`): the institution is part of its
**parent**, which is always made. The **units** are made only if you ask for them: `--units <n>` (or `units` in the message) makes the first *n*, each part of the institution. A file that is there
already is used. `--no-relations` makes neither files nor relations. *Related*, *predecessor* and *successor* institutions of the registry are not used yet.

The topics are no relations yet (**Research Focus** is defined in the ontology): a topic has no place on the computer to be a file in.

## Keys and the polite way

ROR needs no key. **OpenAlex** gives all requests of one address a small daily budget unless there is a free key (openalex.org/settings/api). The plugin takes the address (`email`: OpenAlex's polite
pool) and the key (`api_key`) from `~/config/settings/sen/institution-enricher.settings`, else from the settings of **Paperboy** (`paperboy.settings`), which asks OpenAlex, too: the notification says
if the budget is used up. Without OpenAlex the institution is enriched all the same, only without the counts and the topics.

Every answer is **kept on disk** (`~/config/cache/sen/institution-enricher`; the registry for a week, searches and counts for a day) and the requests are spaced; the program names itself
(`SEN-Campus/0.1 (Haiku; https://sen-labs.org)`) and sends the **name of the institution** (and the ROR id) and the address of the settings, nothing else.

## On the command line

```
SenInstitutionEnricher [-a|--auto] [-w|--wipe] [--units <n>] [--no-relations] [--debug] <file>...
```

## Tests

`tests/run.sh` checks the core (the addresses, the answers of ROR and OpenAlex in `tests/fixtures`, the names) without Haiku: `JSON_INCLUDE=<folder with nlohmann> sh tests/run.sh`.
