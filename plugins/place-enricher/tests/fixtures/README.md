Recorded answers of the real services (fetched 2026-11-06), used by `PlaceCoreTest.cpp`:

* `nominatim-search-*.json`: `search?q=...&format=jsonv2&extratags=1&namedetails=1&addressdetails=1&polygon_text=1` for "Stephansdom Wien" and "Wien" (one answer each).
* `wikidata-Q*.json`: `wbgetentities` of Q5943 (Stephansdom) and Q1741 (Wien); the claims are cut down to the properties that the plugin reads (P18, P94, P41, P242, P158, P154, P625, P131, P17, P31).
* `commons-*.json`: `action=query&prop=imageinfo` of "Wien Wappen.svg" (public domain) and "Wien - Stephansdom (1).JPG" (CC BY-SA 4.0, attribution required).

The data are by OpenStreetMap contributors (ODbL), Wikidata (CC0) and the authors of the Commons files (the licenses are in the answers).
