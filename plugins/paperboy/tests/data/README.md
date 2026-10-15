<!--
SPDX-License-Identifier: CC0-1.0
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# Test data

Answers of the services, shortened (fewer authors, references and locations), as fixtures for `PaperCoreTest.cpp`. The bibliographic data
of OpenAlex is under CC0, Unpaywall's data is open (CC0), too.

| File | From |
|---|---|
| `openalex-nature.json` | `GET https://api.openalex.org/works/https://doi.org/10.1038/nature14539`: a paper of a closed journal with a green copy, no abstract (the publisher does not allow it) |
| `openalex-preprint.json` | `GET https://api.openalex.org/works/https://doi.org/10.48550/arxiv.1706.03762` (OpenAlex answers with a preprint record): abstract, keywords, a direct PDF link |
| `unpaywall-nature.json` | `GET https://api.unpaywall.org/v2/10.1038/nature14539?email=...` |
| `openalex-batch.json` | `GET https://api.openalex.org/works?filter=openalex:W146900863\|W170811313&select=id,doi,title,publication_year` |
| `openalex-search.json` | `GET https://api.openalex.org/works?search=Attention+is+all+you+need&per-page=3&select=id,doi,title,publication_year` |
