<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SUESS, the SEN place enricher

*SUESS* is *Semantic Urban & Environmental Spaces for SEN*, and a bow to Eduard Suess, the Viennese geologist who described the biosphere in *The Face of the Earth*. The folder and the
signature keep the name of what the plugin does (`place-enricher`) like the other plugins; SUESS is its name for people.

Looks a place up at [OpenStreetMap](https://www.openstreetmap.org) (its search service Nominatim) and writes what it finds to the file: the name and the other names, what kind of place it is,
the position, the bounding box, the outline, the address, the links where else the place is known, a short description and a **picture** from Wikidata. Choose **Enrich** on a file of the type
**Place** (or **Location**) in Akita.

> Data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), Open Database License (ODbL). The descriptions come from [Wikidata](https://www.wikidata.org) (CC0), the
> pictures from [Wikimedia Commons](https://commons.wikimedia.org), each with the license of its author (it is written to the file, see below).

**No key is needed.** The public services of OpenStreetMap and Wikimedia are free; this is how they ask to be used, and the plugin does it: it names itself (`SEN-SUESS/0.1 (Haiku; https://sen-labs.org)`),
asks Nominatim **at most once a second** (also when several instances run), **keeps every answer for a week** (`~/config/cache/sen/place-enricher`) so that nothing is asked twice, and has no
autocomplete. It sends the **name of the place** and nothing else that is yours, unless you switch on the lookup by position (below).

## How the place is found

1. A file that was enriched before has its **link to OpenStreetMap** (`schema:sameAs`): that object is looked up again, nothing is searched, nothing is asked.
2. Else, if *Look up files by their position* is **on** and the file has no title but a position (`schema:latitude`, `schema:longitude`: a photo from a camera has them), the **place at that
   position** is looked up. This is **off by default**: the position of your file goes to a third party. (`--position` switches it on for one run.)
3. Else the place is **searched by name**: the title of the file (`dc:title`), else its name without the extension (`Café Central.txt` is "Café Central", an underscore is a blank). If more than one
   place is found you choose (the best first: how well the name fits, how well known the place is, the order of Nominatim; you can switch the question off in the settings of SEN). **`--auto`
   takes the best without asking, but only one that fits the question**: when the name holds what you asked for, what else you asked for must be in its address ("Stephansdom Wien": Wien is;
   "IMG 0001" is not an office called "IMG"). Nothing that does not fit is written.

## What is written

What the file has already is kept; *Enrich (Overwrite)* (Shift) replaces it.

| | Attribute |
|---|---|
| Name, other names (the English one and the alternative names first, at most 12) | `dc:title`, `schema:alternateName` |
| Short description (Wikidata, in your language) | `dc:description` |
| What kind of place | `schema:additionalType`: the tag of OpenStreetMap as IRI, `https://wiki.openstreetmap.org/wiki/Tag:amenity=place_of_worship`; for a city, state or country (an administrative boundary) `Tag:place=city`, which says more than "boundary" |
| Web site | `schema:url` |
| Links (OpenStreetMap, Wikidata, Wikipedia) | `schema:sameAs` (the link to OpenStreetMap is how the place is found again) |
| Position | `schema:latitude`, `schema:longitude` |
| Bounding box | `SEN:boxSouth`, `SEN:boxNorth`, `SEN:boxWest`, `SEN:boxEast` |
| Outline (simplified) | `geo:asWKT`, if *Keep the outline of the place* is on (below) |
| Address (a street or a postcode) | `schema:address` (`Stephansplatz 3, 1010 Wien, Österreich`), `schema:postalCode`, `schema:addressLocality`, `schema:addressRegion`, `schema:addressCountry` (ISO 3166-1) |
| Picture | the thumbnail (`Media:Thumbnail`), `schema:image` (its page at Commons), `schema:license`, `schema:creditText` |

A file with no semantic type gets the type **Place**. The attributes are those of the ontology *SEN Spatial*.

**The outline** is what tells where places are in relation to each other (within, touches, overlaps...): it is asked for within 0.0001 degrees (about 11 metres) of the real one, which is a few
hundred bytes for a building and 17 KB for a city. An outline of more than 256 KB (Austria is 455 KB) is asked for again, once, with a tolerance of 0.5% of the size of the place (10 KB for Austria);
the outline of a place is never kept if it is still larger.

## The picture

If Nominatim names the **Wikidata** item of the place, one request gets its description and the image that it names. For a city, state or country the **coat of arms**, else the flag, a locator map, the
seal, a photo; for any other place the **photo**, else the logo. Two more requests: Commons gives the picture in the size of an icon (256 pixels; a vector image comes as a PNG) with its **license and
author**, and the picture itself. A picture that asks to name its author (CC BY, CC BY-SA) is **credited** in `schema:creditText` (`C.Stadler/Bwag, CC BY-SA 4.0 (Wikimedia Commons)`) and its page is in
`schema:image`. A place that has no Wikidata item has no picture and no description, and is enriched all the same. If the file has a thumbnail already it is not asked for.

## Settings

The Settings window of SEN shows them (*Plugins*, *Enrich*, *SEN SUESS*); they are kept in `~/config/settings/sen/suess.settings` (lines of `key = value`). Whether to ask which place is meant, is a
setting that all plugins share.

| Key | Default | |
|---|---|---|
| `language` | the system's | the language of names and descriptions (`de`, `en`, `pt`...) |
| `lookup_by_position` | `no` | look up a file that has a position and no title by that position |
| `picture` | `yes` | the picture of the place as the thumbnail (two more requests) |
| `boundary` | `yes` | keep the outline of the place |

## On the command line

```
SenPlaceEnricher [-a|--auto] [-w|--wipe] [-l|--language de] [-p|--position] [--debug] <file>...
```

## What it does not do

It makes **no relations and no parents**: that a place is *within* Vienna, *touches* another one or *overlaps* it is worked out when you ask for it, by the spatial navigator, from the outlines and
from Wikidata ("located in"); enriching needs one request for the place and, for the picture, three more. It does not geocode anything in bulk (the public service forbids it).

## HTTP and Wikimedia

The requests go through `BaseEnricher`. Wikimedia sends the header `x-analytics:` with an **empty value**, which is valid HTTP (RFC 9110: `field-value = *field-content`). Haiku's HTTP client rejects it
(`BHttpFields` throws an exception on a thread of the session) and the program ends: a bug of Haiku. The requests to the hosts of Wikimedia are therefore made by a small client over a socket
(`common/SocketHttp.h`, shared with the other plugins) until Haiku accepts an empty value.

## Tests

`tests/run.sh` builds and runs the tests of `core/` on any computer (no network: the answers of the services are recorded in `tests/fixtures`, see there); the nlohmann/json headers are found through
`JSON_INCLUDE`.

## Credits

The icon is "Haikunaut" by Arti ([HVIF Store](https://hvif-store.art/)), MIT license.
