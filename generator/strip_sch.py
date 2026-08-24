#!/usr/bin/env python3
"""
Remove symbols, and the wire stubs and net labels that belong to them, from the
hand-edited schematic.

Same rule as patch_sch.py and add_testpoints.py: the file has been edited in KiCad 10, so
it is modified as TEXT.  Nothing that survives is rewritten, so every remaining symbol
position, wire, label and uuid is untouched.

Removing a symbol is more dangerous than adding one.  A symbol on this sheet connects
through a short wire stub carrying a net label -- leave the stub behind and you have an
orphan label still declaring the net; take one stub too many and a real connection
disappears silently.  So the stubs are not hardcoded per part: this script reads each
symbol's pin offsets out of `lib_symbols`, computes the pin positions, removes only wires
that actually touch one, and then removes labels sitting at the far end of those wires.

`verify.py` is the check on all of it -- it rebuilds the netlist geometrically from the
finished file and compares against an independently written expectation.  Run it after.

Usage:  python3 strip_sch.py ../Logging_Current_Meter.kicad_sch
        python3 strip_sch.py ../Logging_Current_Meter.kicad_sch --dry-run
"""

import datetime
import math
import re
import shutil
import sys

TOL = 0.01

# ======================================================================================
#  What to remove
# ======================================================================================
# The 13 display-bus and 4 user-IO probe points.  The 10 analog ones (TP1-TP10) stay --
# they are the section 6 calibration set, and the accuracy story depends on being able
# to measure them.  See DESIGN.md section 9 for what loses probe access.
REMOVE = [f"TP{i}" for i in range(11, 28)] + ["C12", "C13"]

# J9-J12 sat after TP27 on the block K grid.  With TP11-TP27 gone they would be stranded
# three rows below the survivors, so they move up into the freed slots.
#
# The grid ORIGIN is read from TP1's actual position, not hardcoded.  Block K has been
# dragged as a unit in KiCad since add_testpoints.py ran -- every item is offset by
# (-198.12, -53.34) from the coordinates that script computed.  Hardcoding the original
# numbers would have flung these four symbols back across the sheet, away from the block
# they belong to, and silently undone hand placement that DESIGN.md section 12 exists to
# protect.  Only the pitch is a constant; the origin is discovered.
XP, YP, COLS = 25.4, 8.89, 7
STUB = 5.08
GRID_ANCHOR = "TP1"          # k = 0 of the block K grid
REPOSITION = {"J9": 10, "J10": 11, "J11": 12, "J12": 13}

# Block K shrinks from five rows to two.  The rectangle is found by looking for the one
# that encloses the anchor, so it too survives having been moved.
BLOCK_ROWS_AFTER = 2


def span(text, start):
    """Paren-balanced span beginning at `start`, string-aware."""
    d, i = 0, start
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                if text[i] == "\\":
                    i += 1
                i += 1
        elif c == "(":
            d += 1
        elif c == ")":
            d -= 1
            if d == 0:
                return start, i + 1
        i += 1
    raise ValueError("unbalanced")


def lib_pins(text):
    """{lib_id: [(dx, dy), ...]} -- pin connection points, symbol-local."""
    out = {}
    for m in re.finditer(r'\(symbol "([^"]+)"\n', text):
        name = m.group(1)
        if ":" not in name:
            continue
        s, e = span(text, m.start())
        body = text[s:e]
        pins = [(float(p.group(1)), float(p.group(2)))
                for p in re.finditer(
                    r'\(pin \w+ \w+\s*\n\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)', body)]
        if pins:
            out[name] = pins
    return out


def find_placed(text):
    """{ref: (start, end, x, y, rot, lib_id)} for every top-level placed symbol."""
    placed = {}
    for m in re.finditer(r"\n\t\(symbol\n", text):
        s, e = span(text, m.start() + 1)
        blk = text[s:e]
        ref = re.search(r'\(property\s*\n?\s*"Reference"\s*\n?\s*"([^"]+)"', blk)
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+) (\d+)\)", blk)
        lib = re.search(r'\(lib_id "([^"]+)"\)', blk)
        if ref and at and lib:
            placed[ref.group(1)] = (s, e, float(at.group(1)), float(at.group(2)),
                                    int(at.group(3)), lib.group(1))
    return placed


def remove_symbols(text, refs, verbose=True):
    """Delete symbols together with the wire stubs and net labels that belong to them.

    Stub geometry is never hardcoded: each symbol's pin offsets are read out of
    `lib_symbols`, only wires that actually end on a pin point are removed, and then only
    labels sitting at the far end of those wires.  Returns the new text.
    """
    pins_by_lib = lib_pins(text)
    placed = find_placed(text)

    kill_points = []
    for ref in refs:
        if ref not in placed:
            continue
        _, _, sx, sy, rot, lib = placed[ref]
        if rot != 0:
            raise SystemExit(f"{ref} is rotated {rot} deg; this script only handles 0")
        for dx, dy in pins_by_lib.get(lib, []):
            # KiCad's schematic Y axis points down, so a pin at local +dy appears at
            # sy - dy.  Both signs are collected rather than guessed: a stub is only
            # ever removed if a wire actually ends on the point.
            kill_points.append((sx + dx, sy - dy))
            kill_points.append((sx + dx, sy + dy))

    def near(a, b):
        return math.isclose(a[0], b[0], abs_tol=TOL) and math.isclose(a[1], b[1],
                                                                     abs_tol=TOL)

    # ---- collect wires touching those points, and the labels at their far ends --------
    cuts = []          # (start, end) spans to delete
    far_ends = []
    for m in re.finditer(
            r"\t\(wire\n\t\t\(pts\n\t\t\t\(xy ([-\d.]+) ([-\d.]+)\) "
            r"\(xy ([-\d.]+) ([-\d.]+)\)", text):
        p1 = (float(m.group(1)), float(m.group(2)))
        p2 = (float(m.group(3)), float(m.group(4)))
        hit1 = any(near(p1, k) for k in kill_points)
        hit2 = any(near(p2, k) for k in kill_points)
        if hit1 or hit2:
            s, e = span(text, m.start())
            cuts.append((s, e))
            far_ends.append(p2 if hit1 else p1)

    n_lab = 0
    for m in re.finditer(r'\t\(label "([^"]+)"\n\t\t\(at ([-\d.]+) ([-\d.]+)', text):
        p = (float(m.group(2)), float(m.group(3)))
        if any(near(p, f) for f in far_ends):
            s, e = span(text, m.start())
            cuts.append((s, e))
            n_lab += 1

    for ref in refs:
        if ref in placed:
            cuts.append((placed[ref][0], placed[ref][1]))

    n_sym = sum(1 for r in refs if r in placed)
    if verbose:
        print(f"removing {n_sym} symbols, {len(cuts) - n_sym - n_lab} wires, "
              f"{n_lab} labels")

    # Delete back to front so earlier offsets stay valid, and swallow the newline that
    # followed each item so the file does not fill up with blank lines.
    for s, e in sorted(cuts, reverse=True):
        while s > 0 and text[s - 1] == "\t":
            s -= 1
        if e < len(text) and text[e] == "\n":
            e += 1
        text = text[:s] + text[e:]
    return text


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    dry = "--dry-run" in sys.argv
    text = open(path, encoding="utf-8").read()

    placed = find_placed(text)
    missing = [r for r in REMOVE if r not in placed]
    if missing:
        print(f"already absent, nothing to do for: {missing}")
        if len(missing) == len(REMOVE):
            return 0

    text = remove_symbols(text, REMOVE)
    placed = find_placed(text)

    # ---- move the rail pads up into the freed grid slots ------------------------------
    if GRID_ANCHOR not in placed:
        raise SystemExit(f"{GRID_ANCHOR} missing; cannot locate the block K grid")
    X0, Y0 = placed[GRID_ANCHOR][2], placed[GRID_ANCHOR][3]
    print(f"block K grid origin, read from {GRID_ANCHOR}: ({X0:.2f}, {Y0:.2f})")

    for ref, k in REPOSITION.items():
        nx = X0 + (k % COLS) * XP
        ny = Y0 + (k // COLS) * YP
        # re-locate: the file has shifted since `placed` was built
        found = None
        for m in re.finditer(r"\n\t\(symbol\n", text):
            s, e = span(text, m.start() + 1)
            blk = text[s:e]
            if re.search(r'\(property\s*\n?\s*"Reference"\s*\n?\s*"' + ref + r'"', blk):
                found = (s, e, blk)
                break
        if not found:
            raise SystemExit(f"{ref} not found for repositioning")
        s, e, blk = found
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+) (\d+)\)", blk)
        ox, oy = float(at.group(1)), float(at.group(2))
        dx, dy = nx - ox, ny - oy
        if abs(dx) < TOL and abs(dy) < TOL:
            continue

        def shift(mm):
            return f"(at {float(mm.group(1)) + dx:g} {float(mm.group(2)) + dy:g} {mm.group(3)})"

        newblk = re.sub(r"\(at ([-\d.]+) ([-\d.]+) (\d+)\)", shift, blk)
        text = text[:s] + newblk + text[e:]

        # its stub and label move with it
        text = text.replace(
            f"(xy {ox:g} {oy:g}) (xy {ox + STUB:g} {oy:g})",
            f"(xy {nx:g} {ny:g}) (xy {nx + STUB:g} {ny:g})")
        text = re.sub(
            r'(\t\(label "[^"]+"\n\t\t\(at )' + f"{ox + STUB:g} {oy:g}",
            r"\g<1>" + f"{nx + STUB:g} {ny:g}", text)
        print(f"  {ref}: ({ox:.2f},{oy:.2f}) -> ({nx:.2f},{ny:.2f})")

    # ---- shrink block K ---------------------------------------------------------------
    want_bottom = Y0 + (BLOCK_ROWS_AFTER - 1) * YP + 6.35
    for m in re.finditer(r"\(rectangle\n\t\t\(start ([-\d.]+) ([-\d.]+)\)\n\t\t"
                         r"\(end ([-\d.]+) ([-\d.]+)\)", text):
        x1, y1, x2, y2 = (float(g) for g in m.groups())
        if not (min(x1, x2) <= X0 <= max(x1, x2) and min(y1, y2) <= Y0 <= max(y1, y2)):
            continue
        bottom = max(y1, y2)
        if bottom <= want_bottom:
            break
        newy = want_bottom if y2 > y1 else y1
        text = (text[:m.start()]
                + f"(rectangle\n\t\t(start {x1:g} {y1:g})\n\t\t"
                  f"(end {x2:g} {newy:g})"
                + text[m.end():])
        print(f"  block K rectangle: bottom {bottom:.2f} -> {newy:.2f}")
        break

    if dry:
        print("\n-- dry run, nothing written --")
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copy2(path, f"{path}.bak-{stamp}")
    open(path, "w", encoding="utf-8").write(text)
    print(f"\nwrote {path}\nbackup: {path}.bak-{stamp}")
    print("\nNow run:  python3 verify.py ../Logging_Current_Meter.kicad_sch ../LCM.kicad_sym")
    return 0


if __name__ == "__main__":
    sys.exit(main())
