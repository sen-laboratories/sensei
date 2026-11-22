# Robin icon work

`robin-logo_simplified.svg` is the logo as the owner traced it (a samurai archer in a round medallion). HVIF takes 255 paths at most, so the icon has two drawings that share them, chosen by the size
(level of detail = size / 64):

* **under about 19 px: a 16x16 pixel icon**, `robin-pixel-16.svg` (`robinpx.py`: a grid of characters, one for each colour; needs `shapely`).
* **from about 19 px: the logo**, `robin-flat.svg`: `robin-logo_lite.svg` (the logo without the mist ribbons and the right bamboo, which do not show at icon sizes) flattened with `flat.py` (`python3 flat.py in out 0.8 40`):
  155 shapes, 219 paths.

`lodtool robin.hvif robin-pixel-16.svg 99 0 0.3 0  robin-flat.svg 0 0.3 4 0.3` joins them (see `lodtool.cpp`); `robin.hvif` is 26 KB (163 shapes, exactly 255 paths), `robin.iom` the same for Icon-O-Matic.
`robin-renders-haiku.png` shows 128, 64, 48, 32 and 16 px as Haiku draws them. The hex of `robin.hvif` is the `vector_icon` of `Resources.rdef`.
