#!/usr/bin/env python3
"""
Emit the DESIGN.md section 14 region map as ASCII, read from the board.

The hand-maintained version drifted twice -- it was still showing TP27, C12, C13, D2, R18
and the old column grid long after all of those were gone. A map that is generated cannot
lie about the layout it is describing.

Usage:  python3 region_map.py ../Logging_Current_Meter.kicad_pcb
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp
from verify_pcb import fp_ref

ORIGIN = (50.0, 50.0)
BOARD_W, BOARD_H = 95.0, 62.0
COLS, ROWS = 76, 27


def main(path):
    root = sexp.loads(open(path).read())
    parts = []
    for fp in sexp.findall(root, "footprint"):
        at = sexp.find(fp, "at")
        parts.append((fp_ref(fp), at[1] - ORIGIN[0], at[2] - ORIGIN[1]))

    grid = [[" "] * COLS for _ in range(ROWS)]

    def place(text, u, v):
        c = int(round(u / BOARD_W * (COLS - 1)))
        r = int(round(v / BOARD_H * (ROWS - 1)))
        c = max(0, min(COLS - len(text), c - len(text) // 2))
        r = max(0, min(ROWS - 1, r))
        # At most one row of give.  A label allowed to drift further than that stops
        # describing where the part is, which is the only thing this map is for -- the
        # first version slid D1 three rows and read as though it were up by the test
        # points.  Anything that will not fit within +/-1 row is reported, not moved.
        for dr in (0, -1, 1):
            rr = r + dr
            if not (0 <= rr < ROWS):
                continue
            span = slice(max(0, c - 1), min(COLS, c + len(text) + 1))
            if all(ch == " " for ch in grid[rr][span]):
                for i, ch in enumerate(text):
                    grid[rr][c + i] = ch
                return True
        return False

    dropped = []
    # Big parts first so they win their cell, then top-to-bottom, left-to-right.
    order = sorted(parts, key=lambda p: (p[0][0] not in "UJH", round(p[2]), p[1]))
    for ref, u, v in order:
        if not place(ref, u, v):
            dropped.append(ref)

    print(f"  u=0{' ' * (COLS - 12)}u=95")
    print("   +" + "-" * COLS + "+ v=0")
    for r, row in enumerate(grid):
        print("   |" + "".join(row) + "|")
    print("   +" + "-" * COLS + "+ v=62")
    print(f"        CURRENT BLOCK  u <= 42.5        "
          f"ELECTRONICS  u >= 43")
    if dropped:
        print(f"\n  (too crowded to label: {', '.join(dropped)})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_pcb")
