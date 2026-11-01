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
| `arxiv-search.xml` | `GET https://export.arxiv.org/api/query?search_query=ti:"Attention Is All You Need"&max_results=2` (Atom, as arXiv answers it; arXiv metadata is CC0) |
| `arxiv-id-old.xml` | `GET https://export.arxiv.org/api/query?id_list=hep-th/9711200&max_results=1`: an old style identifier, a journal reference and a DOI of the journal |
| `semanticscholar-search.json`, `dblp-search.json`, `dblp-none.json` | **written by hand after the documented answers** of `GET https://api.semanticscholar.org/graph/v1/paper/search?...` and `GET https://dblp.org/search/publ/api?...&format=json`: Semantic Scholar answers `429` to every request without a key at the time of writing, and DBLP puts a challenge for bots (Anubis) in front of its API, so no real answer could be saved. The data in them is made up |
| `orcid-person.json`, `orcid-employments.json` | `GET https://pub.orcid.org/v3.0/0000-0002-9322-3515/person` (biography cut to its first paragraph, fewer keywords, no addresses) and `/employments` (public data of ORCID, CC0) |
| `wikidata-search.json`, `wikidata-entity.json` | `GET https://www.wikidata.org/w/api.php?action=query&list=search&srsearch=haswbstatement:P496=0000-0002-9322-3515` and `?action=wbgetentities&ids=Q3572699&props=claims|descriptions|labels|sitelinks` (claims cut to P18, P569, P856, P496, P108, P106; CC0) |
| `commons-image.json` | `GET https://commons.wikimedia.org/w/api.php?action=query&titles=File:Yoshua Bengio 2019 cropped.jpg&prop=imageinfo&iiprop=url|extmetadata&iiurlwidth=256` (the license of the photo is CC BY 4.0, by Maryse Boyce) |
