#!/usr/bin/env python3
# robinpx.py <out.svg> [variant]: Robin, the samurai archer in the green medallion, as a 16x16 pixel icon
import sys, math
pal = {'G': '#2f4a35', 'g': '#5f7f52', 'l': '#b7c59a', 'K': '#1d2733', 'H': '#e0a93a', 'S': '#e0ab7e', 'R': '#c0612f', 'B': '#8a4a29', 'W': '#f4f1e7', 'k': '#2e3b4a'}
N = 16
g = [['.'] * N for _ in range(N)]
for y in range(N):
    for x in range(N):
        d = math.hypot(x - 7.5, y - 7.5)
        if d <= 8.0: g[y][x] = 'G' if d > 7.0 else 'l'
def put(cells, ch):
    for x, y in cells:
        if g[y][x] != '.': g[y][x] = ch
# bamboo at the left, the sun-lit sky behind
pass
# the bow on the right
put([(11, 2), (12, 3), (13, 4), (13, 5), (13, 6), (13, 7), (13, 8), (13, 9), (12, 10), (11, 11)], 'B')
# the helmet with its crest
put([(6, 1), (9, 1), (7, 2), (8, 2)], 'H'); put([(6, 3), (7, 3), (8, 3), (9, 3), (5, 4), (6, 4), (7, 4), (8, 4), (9, 4)], 'K')
# the face looking to the right, the arrow at the height of the eye
put([(6, 5), (7, 5), (8, 5), (6, 6), (7, 6), (8, 6)], 'S'); put([(9, 5)], 'K')
put([(9, 6), (10, 6), (11, 6), (12, 6), (13, 6), (14, 6)], 'W'); put([(14, 6)], 'K')
# the body in armour, the arm drawn out, the hand on the string
put([(4, 7), (5, 7), (6, 7), (7, 7), (8, 7), (9, 7), (10, 7), (11, 7)], 'K'); put([(12, 7)], 'S')
put([(4, 8), (5, 8), (6, 8), (7, 8), (8, 8), (9, 8)], 'K'); put([(5, 8), (8, 8)], 'R')
for y in range(9, 14): put([(x, y) for x in range(5, 10)], 'K')
put([(5, 10), (6, 10), (7, 10), (8, 10), (9, 10)], 'R'); 
rows = [''.join(r) for r in g]
from shapely.geometry import box
from shapely.ops import unary_union
cells = {}
for y, row in enumerate(rows):
    for x, c in enumerate(row):
        if c != '.': cells.setdefault(c, []).append(box(x, y, x + 1, y + 1))
def ring(c): return 'M' + ' L'.join('%d %d' % p for p in list(c.coords)[:-1]) + 'Z'
o = ['<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">']
for c in 'GlgBkKRSHW':
    if c in cells:
        u = unary_union(cells[c]); polys = [u] if u.geom_type == 'Polygon' else list(u.geoms)
        d = ''
        for p in polys: d += ring(p.exterior) + ''.join(ring(i) for i in p.interiors)
        o.append('<path fill-rule="evenodd" d="%s" fill="%s"/>' % (d, pal[c]))
o.append('</svg>'); open(sys.argv[1], 'w').write('\n'.join(o))
print('\n'.join(rows))
