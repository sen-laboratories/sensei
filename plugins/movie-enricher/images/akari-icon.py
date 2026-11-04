#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
"""
The application icon of Akari (the movie enricher of SEN): a simplified drawing of its logo, a film projector that throws light onto a document with a graph of three nodes.
Filled paths only (no strokes, no gradients), a darker shape under each one gives the outline, so that sen-core/tools/svg2hvif.py can make the vector icon.

    akari-icon.py > akari-icon.svg
    ../../../../sen-core/tools/svg2hvif.py akari-icon.svg --no-fit --rdef      the resource vector_icon for Resources.rdef
"""
import math

INK = '#2b303a'


def circle(cx, cy, r):
    k = 0.5523 * r
    return (f'M{cx - r:.2f} {cy:.2f} C{cx - r:.2f} {cy - k:.2f} {cx - k:.2f} {cy - r:.2f} {cx:.2f} {cy - r:.2f} '
            f'C{cx + k:.2f} {cy - r:.2f} {cx + r:.2f} {cy - k:.2f} {cx + r:.2f} {cy:.2f} '
            f'C{cx + r:.2f} {cy + k:.2f} {cx + k:.2f} {cy + r:.2f} {cx:.2f} {cy + r:.2f} '
            f'C{cx - k:.2f} {cy + r:.2f} {cx - r:.2f} {cy + k:.2f} {cx - r:.2f} {cy:.2f} Z')


def poly(*points):
    return 'M' + ' L'.join(f'{x:.2f} {y:.2f}' for x, y in points) + ' Z'


def rect(x, y, w, h):
    return poly((x, y), (x + w, y), (x + w, y + h), (x, y + h))


def edge(a, b, width):
    (x1, y1), (x2, y2) = a, b
    dx, dy = y2 - y1, x1 - x2
    n = math.hypot(dx, dy) / (width / 2)
    dx, dy = dx / n, dy / n
    return poly((x1 + dx, y1 + dy), (x2 + dx, y2 + dy), (x2 - dx, y2 - dy), (x1 - dx, y1 - dy))


def shape(d, fill, opacity=None):
    extra = f' fill-opacity="{opacity}"' if opacity is not None else ''
    return f'  <path d="{d}" fill="{fill}"{extra}/>'


def make():
    o = []
    # the light: from the lens to the document
    o.append(shape(poly((27, 38), (45, 18), (45, 56), (27, 46)), '#ffd86b', 0.55))
    o.append(shape(poly((27, 40), (45, 24), (45, 50), (27, 44)), '#fff3c4', 0.6))

    # the document: outline, paper, folded corner
    o.append(shape('M41.2 4.6 L53.6 4.6 L61.4 12.4 L61.4 57 C61.4 58.7 60.1 60 58.4 60 L41.6 60 C39.9 60 38.6 58.7 38.6 57 L38.6 7.2 C38.6 5.8 39.7 4.6 41.2 4.6 Z', INK))
    o.append(shape('M41.4 6.2 L52.6 6.2 L59.8 13.4 L59.8 56.6 C59.8 57.4 59.2 58.4 58.2 58.4 L41.8 58.4 C40.8 58.4 40.2 57.6 40.2 56.6 L40.2 7.6 C40.2 6.8 40.6 6.2 41.4 6.2 Z', '#f3f5f8'))
    o.append(shape(poly((52.6, 6.2), (59.8, 13.4), (52.6, 13.4)), '#aab2c0'))

    # the graph: three nodes and their links
    nodes = [(50.5, 21.5, '#d93030'), (44.4, 38.5, '#2eaa3c'), (55.6, 44, '#2f6fe0')]
    links = [(0, 1), (0, 2), (1, 2)]
    for a, b in links:
        o.append(shape(edge(nodes[a][:2], nodes[b][:2], 3.6), INK))
    for a, b in links:
        o.append(shape(edge(nodes[a][:2], nodes[b][:2], 1.8), '#f0b800'))
    for x, y, color in nodes:
        o.append(shape(circle(x, y, 4.6), INK))
        o.append(shape(circle(x, y, 3.3), color))
        o.append(shape(circle(x - 1.1, y - 1.2, 1.0), '#ffffff', 0.7))

    # the projector: base, body, lens, two reels
    o.append(shape(poly((1.6, 52), (27.5, 52), (30.5, 59), (0, 59)), INK))
    o.append(shape(poly((3.2, 53.4), (26.5, 53.4), (28.2, 57.6), (2, 57.6)), '#8a919c'))
    o.append(shape(rect(3, 34, 24, 19), INK))
    o.append(shape(rect(4.6, 35.6, 20.8, 15.8), '#8a919c'))
    o.append(shape(rect(4.6, 35.6, 20.8, 4), '#aab2bd'))
    o.append(shape(rect(7, 42, 12, 6), '#5d6572'))
    o.append(shape(rect(25, 38, 5.6, 9), INK))
    o.append(shape(rect(25.4, 39.4, 4, 6.2), '#c9a25a'))
    for cx, cy, r in ((10.5, 25.5, 9.2), (22.5, 19.5, 8)):
        o.append(shape(circle(cx, cy, r), INK))
        o.append(shape(circle(cx, cy, r - 1.7), '#b8bec8'))
        for i in range(5):
            a = math.radians(i * 72 + 18)
            o.append(shape(circle(cx + math.cos(a) * r * 0.55, cy + math.sin(a) * r * 0.55, r * 0.2), '#5d6572'))
        o.append(shape(circle(cx, cy, r * 0.2), INK))
    return '\n'.join(o)


print('<?xml version="1.0" encoding="UTF-8"?>')
print('<!-- Akari application icon (64x64): made by akari-icon.py, filled paths for svg2hvif.py -->')
print('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">')
print(make())
print('</svg>')
