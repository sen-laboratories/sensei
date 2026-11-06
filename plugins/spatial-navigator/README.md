<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# The SEN spatial navigator

Works out the **spatial relations between places** when you open them: *within* and *contains*, *covered by* and *covers*, *touches*, *overlaps*, *equal to*. Open the relation at a place (*Open related*,
group *Spatial*): the places that it is related to in that way are the answer, each with the label of its direction (`within` for the place that this one is in, `contains` for a place in it).

The relations are **not stored**. The SEN server asks this plugin (`sensei::cmd::kResolve`, see `Sensei.h` of sento) when the relation is opened, so what you see is right at that moment, also after
you corrected the outline of a place; nothing is worked out for a menu that is only shown. The relation types are those of the ontology *SEN Spatial* (`dynamic`, not stored), the names are those of the
Region Connection Calculus (RCC-8) in the words of everyday use:

| Relation | Seen from the place | Seen from the other one | RCC-8 |
|---|---|---|---|
| within | within | contains | NTPP |
| covered by | covered by | covers | TPP (inside, touching the border) |
| touches | touches | touches | EC |
| overlaps | overlaps | overlaps | PO |
| equal to | equal to | equal to | EQ |

Disjoint (DC) is not a relation: nearly every two places are disjoint.

## Where the answer comes from

* **The outlines and positions of the places that exist** (the files of the type Place and Location, on every volume with queries): of every place whose bounding box meets the one of this place the geometry
  engine **GEOS** (`libgeos_c`, the C API; the only new dependency) gives the DE-9IM matrix of the two geometries, and eight patterns map it to the relation. The outline is the attribute `geo:asWKT`
  that SUESS keeps, else the position (`schema:latitude`, `schema:longitude`) is a point. A point can be within or equal to a place, it cannot touch or overlap one.
* **Wikidata**, for *within*: the places that contain this one ("located in", all of the chain: **Stephansdom** is in **Innere Stadt**, **Vienna** and **Austria**), from its Wikidata link (`schema:sameAs`)
  with **one request** to the query service. A place of that chain that exists as a file (by its Wikidata link) is a target. One that does not is **made in the Library** of the current context
  (`~/Library/<context>/Places`) with its name, link, position, short description and **type** (`country`, `city`, `district`...; the kinds that are told are a short list, in this order of preference:
  country, city, state, county, municipality, district, town, village, region, continent), so that it can be opened; it can be enriched with SUESS like any place. The answer is kept for a month.

## Dependencies

`sen_ontology_core`, `sen_ontology_spatial` and `lib:libgeos_c` (the package `geos` of HaikuPorts, 1.6 MB). The requests to Wikidata go over a socket, see `common/SocketHttp.h`.

## On the command line

```
SenSpatialNavigator <relation type> <place file>      e.g. relation/x-vnd.sen-labs.relation.spatial.within
```

prints what it finds (and makes the parents that are missing).

## Tests

`tests/run.sh` builds and runs the tests of `core/` on any computer: the mapping of the DE-9IM matrices (taken from what GEOS gave on Haiku, also for the real outlines of Stephansdom and Wien), the boxes, the
labels, and the answer of Wikidata (recorded in `tests/fixtures`). The nlohmann/json headers are found through `JSON_INCLUDE`.

## Credits

The icon is "GCompris" by 3dEyes ([HVIF Store](https://hvif-store.art/)), MIT license. Wikidata is in the public domain (CC0).
