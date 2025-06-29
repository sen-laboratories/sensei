<p align="center">
  <img src="images/sensei-logo.jpg" width=360 />
</p>

<h1 align="center">SENSEI</h1>
<h2 align="center">Semantic Extraction, Navigation, Search, Enrichment and Inference</h2>

SENSEI is a core part of SEN and provides an infrastructure for:
* *S*emantic *E*xtraction: extract semantics hidden in files, like structure, entities or relations
* *N*avigation: provide a way to navigate linked data using plugins, integrating with applications for resolving relation properties (e.g. references to pages in a document)
* *S*earch: search for missing metadata using available fragments and keys (ISBN, IMDB id, DOI,...)
* *E*nrichment: query open data sources for metadata and enrich local files, e.g. Book or Movie entities, PDF papers,...
* *I*dentification: determine the semantic type of a given file, e.g. a Paper, Invoice or ebook from a PDF document

SEN uses native queries for plugins supporting a given FileType and Feature (any combination of the above).
The plugin gets called with the original file reference and all properties of the relation.

More documentation is coming up soon and will be available in the Wiki.
