# Specification: SENSEI Academic Paper Metadata & Graph Extension Plugin

## 1. Overview & Objectives

The **SENSEI Academic Paper Plugin** extends the SEN (Semantic Extensions Native) desktop ecosystem by enabling semantic metadata extraction, open-access source enrichment, and graph relation building directly on files stored in the filesystem.

The plugin processes academic papers (PDF, EPUB) through two coordinated components:
1. **SENSEI Enricher**: Identifies DOIs, normalizes file names, fetches metadata, and maps open-access download/landing URLs via Unpaywall.
2. **SENSEI Extractor**: Queries OpenAlex for bibliographic details, author affiliations, keywords, and reference graphs, mapping them directly to filesystem extended attributes (`xattr`) and `SEN:relation` links.

---

## 2. System Architecture & Workflow Pipeline

```
[ Local Paper (PDF / EPUB) ]
             │
             ▼
   ┌───────────────────┐
   │  Local Extractor  │ ──► Parse text / Regex DOI (Fallback: GROBID/PdfMiner)
   └─────────┬─────────┘
             │ DOI
             ├──────────────────────────┐
             ▼                          ▼
   ┌───────────────────┐      ┌───────────────────┐
   │ SENSEI Enricher   │      │ SENSEI Extractor  │
   │  (Unpaywall API)  │      │  (OpenAlex API)   │
   └─────────┬─────────┘      └─────────┬─────────┘
             │                          │
             │ Metadata & OA Links      │ Author, Concepts, References
             ▼                          ▼
   ┌──────────────────────────────────────────────┐
   │           SEN File System Layer              │
   │  - Extended Attributes (schema:*, dcterms:*) │
   │  - Semantic Relations (SEN:relation)         │
   │  - Optional File Rename Strategy             │
   └──────────────────────────────────────────────┘
```

---

## 3. Data Schema & Metadata Mapping

Attributes are written as filesystem extended attributes (`xattr`) using standard JSON / JSON-LD schemas (`schema:`, `dcterms:`, `sen:`).

### 3.1 Base Metadata Attributes

| Target xattr Key | Schema Vocabulary | Source Field | Example Value |
| :--- | :--- | :--- | :--- |
| `sen.schema:type` | `schema:type` | Hardcoded / OpenAlex `type` | `"ScholarlyArticle"` |
| `sen.dcterms:identifier` | `dcterms:identifier` | DOI | `"10.1016/j.isprsjprs.2023.01.002"` |
| `sen.schema:name` | `schema:name` | OpenAlex / Unpaywall `title` | `"Semantic Integration of Biodiversity Data"` |
| `sen.schema:datePublished`| `schema:datePublished` | Unpaywall/OpenAlex `publication_date` | `"2025-04-12"` |
| `sen.schema:keywords` | `schema:keywords` | OpenAlex `concepts[].display_name` | `["semantics", "geospatial", "biodiversity"]` |
| `sen.schema:abstract` | `schema:abstract` | OpenAlex `abstract_inverted_index` | Reconstructed string text |
| `sen.schema:author` | `schema:author` | OpenAlex `authorships` | Array of objects (Name, ORCID, Affiliation) |

### 3.2 Access Sources & Encodings (`sen.schema:encoding`)

Full-text access URLs and open-access metadata from Unpaywall are stored as a JSON array in `sen.schema:encoding`:

```json
[
  {
    "@type": "MediaObject",
    "contentUrl": "https://journals.example.org/article.pdf",
    "encodingFormat": "application/pdf",
    "sen:source": "unpaywall",
    "sen:oaStatus": "gold",
    "sen:isBest": true
  },
  {
    "@type": "MediaObject",
    "contentUrl": "https://arxiv.org/pdf/2401.12345.pdf",
    "encodingFormat": "application/pdf",
    "sen:source": "unpaywall",
    "sen:version": "submittedVersion"
  }
]
```

**Shortcut Attributes for Fast Search/Indexing:**
* `sen.schema:url`: Primary landing page (`best_oa_location.url_for_landing_page` or DOI URL)
* `sen.pdf_url`: Direct PDF download link (`best_oa_location.url_for_pdf`)
* `sen.oa_status`: Unpaywall `oa_status` (`"gold"`, `"green"`, `"bronze"`, `"hybrid"`, `"closed"`)

### 3.3 Semantic Graph Relations (`SEN:relation`)

Outgoing citation links and incoming references are mapped into the native `SEN:relation` attribute array:

```json
[
  {
    "type": "schema:citation",
    "target": "doi:10.1000/182",
    "properties": {
      "dc:title": "Knowledge Graphs in Environmental Science",
      "openalex_id": "https://openalex.org/W3124567890",
      "sen:resolved_local_file": "file:///path/to/local/paper_182.pdf"
    }
  },
  {
    "type": "dcterms:isReferencedBy",
    "target": "doi:10.1000/99999",
    "properties": {
      "sen:citation_count": 42
    }
  }
]
```

---

## 4. API Endpoints & Request Logic

### 4.1 Unpaywall API (Enricher)
* **Endpoint**: `https://api.unpaywall.org/v2/{doi}?email={user_email}`
* **Method**: `GET`
* **Purpose**: Primary DOI lookup for Open Access status, license type, and direct PDF/EPUB URLs.

### 4.2 OpenAlex API (Extractor)
* **Work Lookup**: `https://api.openalex.org/works/https://doi.org/{doi}`
* **Method**: `GET`
* **Purpose**: Bibliographic details, reconstructed abstracts, author affiliations, concepts/keywords, and referenced works (`referenced_works` array).

---

## 5. Functional Component Specifications

### 5.1 Local DOI Extractor Module
1. Read first 2–3 pages of PDF / EPUB text using local text extraction.
2. Execute RegEx for DOI matching: `10\.\d{4,9}/[-._;()/:A-Z0-9]+` (case-insensitive).
3. If no match found:
   * Option A: Query OpenAlex via Title search (`https://api.openalex.org/works?filter=title.search:{title}`).
   * Option B: Flag file as `sen.status = "doi_not_found"`.

### 5.2 SENSEI Enricher Module
1. Input: Local File Path + Extracted DOI.
2. Perform GET request to Unpaywall API.
3. Extract `best_oa_location`, `oa_status`, `is_oa`, and `title`.
4. Construct `schema:encoding` JSON structure.
5. Write metadata to filesystem `xattr`.
6. **File Renaming Strategy** (Optional / User-Configurable):
   * Pattern: `{year}_{first_author}_{sanitized_title}.pdf`
   * Example: `2025_Doe_Semantic_Integration_of_Biodiversity_Data.pdf`

### 5.3 SENSEI Extractor Module
1. Input: Local File Path + Extracted DOI.
2. Perform GET request to OpenAlex API.
3. Parse abstract from `abstract_inverted_index` (reverse token position lookup).
4. Extract `concepts` (filtered by score > 0.3) and write to `sen.schema:keywords`.
5. Extract `referenced_works` (DOIs / OpenAlex IDs).
6. Local File Resolution:
   * Scan SEN index / directory for existing files containing matching `dcterms:identifier`.
   * Attach `sen:resolved_local_file` file URI if target exists locally.
7. Append formatted relations to `SEN:relation` xattr.

---

## 6. Implementation Guidelines for Flash in Antigravity

When prompting Flash in Antigravity to generate code from this spec, instruct it to:
1. Implement clean separation between the **API Clients** (Unpaywall, OpenAlex), the **Metadata Parser**, and the **SEN FS xattr Writer**.
2. Provide fallback mechanisms for rate limiting (Unpaywall polite pool via mail, OpenAlex `mailto` query param).
3. Ensure thread-safe or asynchronous processing when scanning directory batches.
4. Keep the abstract reconstruction logic decoupled so it can be unit-tested independently.
