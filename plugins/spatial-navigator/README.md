<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# The SEN spatial navigator

Works out the **spatial relations between places** when you open them: *within* and *contains*, *covered by* and *covers*, *touches*, *overlaps*, *equal to*, and the *points of interest* in the area of a
place. Open the relation at a place (*Open related*, group *Spatial*): the places that it is related to in that way are the answer, each with the label of its direction (`within` for the place that
this one is in, `contains` for a place in it).

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

**Nearby** (`nearby`, the same seen from the other place) are the named things that OpenStreetMap knows around a place: shops, cafés, sights, stops, parks, buildings with a name; not the roads (a
street comes in dozens of pieces that all have its name) and not the borders. Each has the **label** of what it is (`supermarket`, `cafe`, `place of worship`; the name if the tags say nothing) and
its **Distance (m)** from the centre of the place you opened the relation at (`SEN:REL:Distance`, a number, so a view of the relation sorts by it). The area is the bounding box of the place, at least
twice the *Search radius* across (a Location or a building is a point: what is around it is meant), and **at most 3 km across**: a district has thousands of named things, a city hundreds of thousands,
which no one can read, so a larger place has none. The answer is the nearest to the middle of the place, at most **40** (setting *Most things nearby*, 5 to 200), where a notable one (a Wikidata item or
Wikipedia article: a church, a museum) counts as three times nearer than a shop, so that the sights of a square come before its shops. The *Search radius* is 50 m to 1.5 km, 250 m by default. The *Map style* is the default style of the map of a place in Shoji (OpenTopoMap, the standard map, Humanitarian or CyclOSM: public servers, no key); a view of the map can switch to another style with the secondary mouse button. What OpenStreetMap said is kept for 24 hours (*Keep answers for*, 1 to 168 hours: places are not remapped that often); a folder of what is nearby that is opened later asks again, and is answered from the kept answer until it is that old. The relation
is *remote*: the views of relation folders (*All related*) do not ask for it until you enter its folder, only opening it does.

A place that has **no position yet** (one that was made with an address only) has nothing near it to find. With the setting *Enrich automatically on demand* (settings of SEN, *Plugins > Enrich*, on by default) the navigator has SUESS look up the address first and works the relation out afterwards; switched off, a notification says that the place has no position, so that an empty folder is not taken for "nothing is there".

A thing that has a file of the type Place (by its OpenStreetMap link, `schema:sameAs`) is that file. One that has not is **made in the Library** (`~/Library/<context>/Places`) with its name (in the language
of the system if the thing has it), its OpenStreetMap link (and the Wikidata link, if it has one), position, **kind** (`supermarket`, `cafe`, `place of worship`, from the tags `amenity`, `shop`, `tourism`...,
and the tag as `schema:additionalType`), web site and address; the first twelve of them that have a Wikidata item are enriched in the background by SUESS, which finds their **picture** (the photo, the coat of arms) and outline; for the others, enrich the ones you want (OpenStreetMap has no picture of its own). Two things of one name
(a supermarket chain) are told apart by the number at OpenStreetMap in the name of the file. The source of the relation (`prov:wasAttributedTo`) is the Overpass API.

**What a relation says about itself.** A place that the shapes were compared for has the property **Kind** (`SEN:REL:Kind`), the name of the relation in words (`non-tangential proper part`, `tangential proper part`, `externally connected`,
`partially overlapping`, `equal`; the opposite directions of the first two say `(inverse)`), and the hidden property **RCC-8** (`SEN:REL:Rcc8`), the code that the comparison gave (`NTPP`, `NTPPi`, `TPP`, `TPPi`, `EC`,
`PO`, `EQ`), which is what a program asks for. A place that Wikidata says contains this one ("located in") has neither: no shapes were compared, and "located in" says nothing about the border. Where a relation comes from is its
**provenance** (`prov:wasAttributedTo`): this plugin's signature for what it worked out from the shapes, `https://www.wikidata.org/wiki/Property:P131` for what Wikidata said. That is also why a place at the edge of its parent
(a district of Vienna that reaches the city limit: really *covered by*) can be in the list from Wikidata only: the simplified outlines of the two cross a little, which the shapes call *overlaps*.

**Opening a place of a relation** is the same from everywhere: a double click on the file of the relation in its folder, and a click on the place in the menu of the relation, both open the place with the application of its type (Shoji,
with the map of the place). The file of a relation names its place (`SEN:REL:TRG`); the item of a menu is the place itself.

**What Wikidata says wins over the outlines for touch and overlap.** If Wikidata says that one place is located in the other, *touches* and *overlaps* are left out for that pair (the outlines of a district at the limit of its
city cross a little because they are simplified, and the shapes would say *overlaps* for a place that is inside); *within*, *covered by* and *equal* stay.

## Where the answer comes from

* **The outlines and positions of the places that exist** (the files of the type Place and Location, on every volume with queries): of every place whose bounding box meets the one of this place the geometry
  engine **GEOS** (`libgeos_c`, the C API; the only new dependency) gives the DE-9IM matrix of the two geometries, and eight patterns map it to the relation. The outline is the attribute `geo:asWKT`
  that SUESS keeps, else the position (`schema:latitude`, `schema:longitude`) is a point. A point can be within or equal to a place, it cannot touch or overlap one.
* **Wikidata**, for *within*: the places that contain this one ("located in", all of the chain: **Stephansdom** is in **Innere Stadt**, **Vienna** and **Austria**), from its Wikidata link (`schema:sameAs`)
  with **one request** to the query service. A place of that chain that exists as a file (by its Wikidata link) is a target. One that does not is **made in the Library** of the current context
  (`~/Library/<context>/Places`) with its name, link, position, short description and **type** (`country`, `city`, `district`...; the kinds that are told are a short list, in this order of preference:
  country, city, state, county, municipality, district, town, village, region, continent), so that it can be opened; it can be enriched with SUESS like any place. The answer is kept for a month.

* **OpenStreetMap through the Overpass API**, for *nearby* (the server is chosen in the settings, see below): one request for the box (`nwr[name](south,west,north,east); out center;`; note that Overpass takes south, west, north, east, and
  Nominatim south, north, west, east), kept for a day. The public server works out an answer for half a minute when it is busy, so the first time takes long; the SEN server waits 90 seconds for a
  navigator. The data are by OpenStreetMap contributors (ODbL).

## Settings

In the settings window of SEN, under the spatial navigator: **Overpass server**, the server that is asked for what is nearby. The public server (`overpass-api.de`, the default) is often busy
(half a minute for an answer is common), so there are mirrors that give the same data with other limits:

| Choice | Address | Key |
|---|---|---|
| overpass-api.de | `https://overpass-api.de/api/interpreter` | no. The default; under 10,000 queries a day, often busy |
| Private.coffee | `https://overpass.private.coffee/api/interpreter` | no. No limit |
| VK Maps | `https://maps.mail.ru/osm/tools/overpass/api/interpreter` | no. No limit |
| NextGIS | `https://overpass.nextgis.com/{key}/api/interpreter` | **required** (free, sign up). No limit |
| Overpass.ch | `https://api.overpass.ch/v1/{key}/interpreter` | **required** (free tier of 10,000 requests a month, 30 a minute; paid plans) |
| Custom mirror | the address you type | the flag *The custom mirror requires an API key* says |

The list is the public instances of the [OpenStreetMap wiki](https://wiki.openstreetmap.org/wiki/Overpass_API#Public_Overpass_API_instances) that have no other limits than their capacity and cover the
world; others (Geofabrik, Tracestrack, Overspan: paid; Europe or one country only: dresel.systems, overpass.osm.ch) and a server of your own (`http://localhost:12345/api/interpreter`) are a custom mirror.

**The API key** is entered in a field that is enabled only for a server that *requires* one: NextGIS and Overpass.ch, or the custom mirror when you tick *The custom mirror requires an API key*. For the
servers of the table it is put in the address in place of `{key}`. For a custom mirror the same: write `{key}` in its address (`https://overpass.geofabrik.de/{key}/api/interpreter`), or leave it out and the key
is sent as `Authorization: Bearer <key>`, and only over https. A server that requires a key and has none is not asked: the public server answers instead. The key is kept as typed in
`~/config/settings/sen/spatial-navigator.settings` (no `#`, which starts a comment there). The answer of a question is kept for a day whichever server gave it.

## Dependencies

`sen_ontology_core`, `sen_ontology_spatial` and `lib:libgeos_c` (the package `geos` of HaikuPorts, 1.6 MB). The requests to Wikidata and Overpass go over a socket, see `common/SocketHttp.h`.

## On the command line

```
SenSpatialNavigator <relation type> <place file>      e.g. relation/x-vnd.sen-labs.relation.spatial.within
```

prints what it finds (and makes the parents and nearby places that are missing).

## Tests

`tests/run.sh` builds and runs the tests of `core/` on any computer: the mapping of the DE-9IM matrices (taken from what GEOS gave on Haiku, also for the real outlines of Stephansdom and Wien), the boxes, the
labels, the answer of Wikidata and an answer of Overpass (recorded in `tests/fixtures`). `tests/vm.sh` (squares, no network) and `tests/vm-poi.sh` (the public Overpass server) run on Haiku. The nlohmann/json headers are found through `JSON_INCLUDE`.

## Credits

What is nearby is by [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors (ODbL), asked through the [Overpass API](https://overpass-api.de). The icon is "GCompris" by 3dEyes ([HVIF Store](https://hvif-store.art/)), MIT license. Wikidata is in the public domain (CC0).
