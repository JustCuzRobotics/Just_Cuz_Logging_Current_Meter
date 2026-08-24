#!/usr/bin/env python3
"""
Estimate how much copper a placement will need, so a claim that one layout routes better
than another can be checked instead of asserted.

For every net it builds a minimum spanning tree over the net's pad centres using Manhattan
distance, and sums the edges.  That is the classic placement cost estimate: it is a lower
bound on trace length for a net, it needs no router, and it is monotone in the thing that
actually makes boards hard to route.

GND and the two 60 A bus nets are reported separately and excluded from the total -- they
are poured, not routed, so their MST says nothing useful.

Usage:  python3 routability.py ../Logging_Current_Meter.kicad_pcb [other.kicad_pcb ...]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp
from verify_pcb import pad_boxes, fp_ref

POURED = {"GND", "/PACK+", "/LOAD+", "+3V3", "+5V"}   # In1 plane, bus channels, In2 power


def net_pads(path):
    root = sexp.loads(open(path).read())
    nets = {}
    for fp in sexp.findall(root, "footprint"):
        ref = fp_ref(fp)
        for name, net, ptype, x0, y0, x1, y1 in pad_boxes(fp):
            if net is None:
                continue
            nets.setdefault(net, []).append(
                (f"{ref}.{name}", (x0 + x1) / 2, (y0 + y1) / 2))
    return nets


def mst(points):
    """Prim, Manhattan metric.  Returns total edge length."""
    if len(points) < 2:
        return 0.0
    inside = [0]
    outside = list(range(1, len(points)))
    total = 0.0
    while outside:
        best = None
        for i in inside:
            for j in outside:
                d = (abs(points[i][1] - points[j][1])
                     + abs(points[i][2] - points[j][2]))
                if best is None or d < best[0]:
                    best = (d, j)
        total += best[0]
        inside.append(best[1])
        outside.remove(best[1])
    return total


def report(path):
    nets = net_pads(path)
    rows = []
    for n, pts in nets.items():
        rows.append((n, len(pts), mst(pts)))
    routed = [r for r in rows if r[0] not in POURED]
    total = sum(r[2] for r in routed)
    return total, sorted(routed, key=lambda r: -r[2]), rows


def main(paths):
    results = {}
    for p in paths:
        results[p] = report(p)

    for p in paths:
        total, routed, _ = results[p]
        print(f"{os.path.basename(p)}")
        print(f"   routed nets: {len(routed)}   total MST: {total:.1f} mm")

    if len(paths) == 2:
        a, b = paths
        ta, ra, _ = results[a]
        tb, rb, _ = results[b]
        da = dict((n, l) for n, _, l in ra)
        db = dict((n, l) for n, _, l in rb)
        print(f"\n{'net':18s} {'before':>9s} {'after':>9s} {'change':>9s}")
        deltas = sorted(((n, da.get(n, 0), db.get(n, 0)) for n in set(da) | set(db)),
                        key=lambda t: t[2] - t[1])
        for n, x, y in deltas:
            if abs(y - x) < 0.05:
                continue
            print(f"{n:18s} {x:8.1f}mm {y:8.1f}mm {y-x:+8.1f}mm")
        print(f"\n{'TOTAL':18s} {ta:8.1f}mm {tb:8.1f}mm {tb-ta:+8.1f}mm "
              f"({100*(tb-ta)/ta:+.1f}%)")


if __name__ == "__main__":
    main(sys.argv[1:] or ["../Logging_Current_Meter.kicad_pcb"])
