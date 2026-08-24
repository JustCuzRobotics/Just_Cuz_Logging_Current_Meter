#!/usr/bin/env python3
"""
Remove J6 (the 2.54 mm display header, superseded by the J7 FPC connector) and add
mounting holes.

J6 removal is done by COORDINATE, not by text search: the symbol's 14 pin positions are
computed from the library geometry, then the wire stubs starting at those points and the
labels sitting at those stubs' far ends are deleted along with the symbol. Deleting the
symbol alone would strand 14 labels, which still create nets and would leave the display
signals looking connected to nothing.

Mounting holes are zero-pin symbols so they live in the netlist and survive
"Update PCB from Schematic" with 'delete extra footprints' enabled.
  H1-H4  board corners, M3
  H5-H8  display module standoffs -- POSITIONS TO BE SET once the module is measured
"""

import re
import shutil
import sys
import uuid as _uuid
import datetime

sys.path.insert(0, ".")
from sexp import loads, find, findall

TAB = "\t"
TOL = 0.01
STUB = 5.08


def U():
    return str(_uuid.uuid4())


def span_end(text, start):
    depth, i, n = 0, start, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced")


def find_spans(text, token):
    for m in re.finditer(r"\(" + token + r"\b", text):
        s = m.start()
        try:
            yield s, span_end(text, s)
        except ValueError:
            continue


def near(a, b):
    return abs(a - b) < TOL


MOUNT_SYMBOL = f"""{TAB}{TAB}(symbol "LCM:MountingHole"
{TAB}{TAB}{TAB}(exclude_from_sim yes)
{TAB}{TAB}{TAB}(in_bom no)
{TAB}{TAB}{TAB}(on_board yes)
{TAB}{TAB}{TAB}(in_pos_files no)
{TAB}{TAB}{TAB}(duplicate_pin_numbers_are_jumpers no)
{TAB}{TAB}{TAB}(property "Reference" "H"
{TAB}{TAB}{TAB}{TAB}(at 0 3.81 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(property "Value" "MountingHole"
{TAB}{TAB}{TAB}{TAB}(at 0 -3.81 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(property "Footprint" ""
{TAB}{TAB}{TAB}{TAB}(at 0 -6.35 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(property "Datasheet" ""
{TAB}{TAB}{TAB}{TAB}(at 0 -8.89 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(property "Description" "Mechanical mounting hole, no electrical connection"
{TAB}{TAB}{TAB}{TAB}(at 0 0 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(symbol "MountingHole_0_1"
{TAB}{TAB}{TAB}{TAB}(circle
{TAB}{TAB}{TAB}{TAB}{TAB}(center 0 0)
{TAB}{TAB}{TAB}{TAB}{TAB}(radius 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB}(stroke
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(width 0.254)
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type default)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(fill
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type none)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}(circle
{TAB}{TAB}{TAB}{TAB}{TAB}(center 0 0)
{TAB}{TAB}{TAB}{TAB}{TAB}(radius 2.032)
{TAB}{TAB}{TAB}{TAB}{TAB}(stroke
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(width 0.254)
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type default)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(fill
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type none)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB})
"""


def mount_instance(ref, value, x, y, root_uuid):
    t = TAB
    return f"""{t}(symbol
{t}{t}(lib_id "LCM:MountingHole")
{t}{t}(at {x} {y} 0)
{t}{t}(unit 1)
{t}{t}(body_style 1)
{t}{t}(exclude_from_sim yes)
{t}{t}(in_bom no)
{t}{t}(on_board yes)
{t}{t}(in_pos_files no)
{t}{t}(dnp no)
{t}{t}(uuid "{U()}")
{t}{t}(property "Reference" "{ref}"
{t}{t}{t}(at {x + 3.81} {y - 1.27} 0)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t}{t}(justify left)
{t}{t}{t})
{t}{t})
{t}{t}(property "Value" "{value}"
{t}{t}{t}(at {x + 3.81} {y + 1.27} 0)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.016 1.016)
{t}{t}{t}{t})
{t}{t}{t}{t}(justify left)
{t}{t}{t})
{t}{t})
{t}{t}(property "Footprint" "LCM:MountingHole_M3_3.2mm"
{t}{t}{t}(at {x} {y + 6.35} 0)
{t}{t}{t}(hide yes)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{t}{t}(property "Datasheet" ""
{t}{t}{t}(at {x} {y + 8.89} 0)
{t}{t}{t}(hide yes)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{t}{t}(instances
{t}{t}{t}(project "Logging_Current_Meter"
{t}{t}{t}{t}(path "/{root_uuid}"
{t}{t}{t}{t}{t}(reference "{ref}")
{t}{t}{t}{t}{t}(unit 1)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{t})
"""


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    text = open(path, encoding="utf-8").read()
    root_uuid = re.search(r'\(uuid "([0-9a-f-]{36})"\)', text).group(1)

    # ---- locate J6 and compute its pin coordinates ------------------------------------
    lib = loads(open("../LCM.kicad_sym", encoding="utf-8").read())
    disp = next(s for s in findall(lib, "symbol") if str(s[1]) == "DISPLAY_14")

    def pins(n, out):
        for p in findall(n, "pin"):
            at = find(p, "at")
            if at:
                out.append((at[1], at[2]))
        for s in findall(n, "symbol"):
            pins(s, out)
    geom = []
    pins(disp, geom)

    j6 = None
    for s, e in find_spans(text, "symbol"):
        body = text[s:e]
        if '(lib_id "LCM:DISPLAY_14")' not in body:
            continue
        if re.search(r'\(property\s+"Reference"\s+"J6"', body):
            at = find(loads(body), "at")
            j6 = (s, e, at[1], at[2])
            break
    if j6 is None:
        print("J6 not found (already removed?)")
        return 0
    s6, e6, jx, jy = j6
    pinpts = [(jx + lx, jy - ly) for lx, ly in geom]
    print(f"J6 at ({jx}, {jy}) with {len(pinpts)} pins")

    # ---- collect the spans to delete: J6 + its stubs + those stubs' labels -------------
    doomed = [(s6, e6)]
    stub_ends = []
    for s, e in find_spans(text, "wire"):
        w = loads(text[s:e])
        pts = [(p[1], p[2]) for p in findall(find(w, "pts"), "xy")]
        if len(pts) != 2:
            continue
        for a, b in (pts, pts[::-1]):
            if any(near(a[0], px) and near(a[1], py) for px, py in pinpts):
                doomed.append((s, e))
                stub_ends.append(b)
                break
    for s, e in find_spans(text, "label"):
        lb = loads(text[s:e])
        at = find(lb, "at")
        if any(near(at[1], x) and near(at[2], y) for x, y in stub_ends):
            doomed.append((s, e))

    print(f"deleting: 1 symbol, {len(stub_ends)} wires, "
          f"{len(doomed) - 1 - len(stub_ends)} labels")
    if len(stub_ends) != 14:
        print(f"  !! expected 14 stubs, found {len(stub_ends)} -- aborting")
        return 1

    for s, e in sorted(doomed, key=lambda t: -t[0]):
        # swallow the trailing newline/indent left behind
        j = e
        while j < len(text) and text[j] in "\n\t ":
            j += 1
        text = text[:s] + text[j:] if text[s - 1:s] == "\t" else text[:s] + text[j:]

    # ---- mounting holes ----------------------------------------------------------------
    if '"LCM:MountingHole"' not in text:
        i = text.index("(lib_symbols")
        j = text.index("\n", i) + 1
        text = text[:j] + MOUNT_SYMBOL + text[j:]

        holes = [("H1", "M3 board"), ("H2", "M3 board"), ("H3", "M3 board"),
                 ("H4", "M3 board"), ("H5", "M3 display TBD"), ("H6", "M3 display TBD"),
                 ("H7", "M3 display TBD"), ("H8", "M3 display TBD")]
        items = []
        for k, (ref, val) in enumerate(holes):
            x = 205.74 + (k % 4) * 20.32
            y = 313.69 + (k // 4) * 12.7
            items.append(mount_instance(ref, val, x, y, root_uuid))
        items.append(f"""{TAB}(text "L  MOUNTING (mechanical only -- H5-H8 positions set at layout)"
{TAB}{TAB}(at 205.74 306.07 0)
{TAB}{TAB}(effects
{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}(size 1.778 1.778)
{TAB}{TAB}{TAB}{TAB}(bold yes)
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(justify left)
{TAB}{TAB})
{TAB}{TAB}(uuid "{U()}")
{TAB})
""")
        k = text.index("\t(sheet_instances")
        text = text[:k] + "".join(items) + text[k:]
        print("added H1-H8 mounting holes")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copy2(path, f"{path}.bak-{stamp}")
    open(path, "w", encoding="utf-8").write(text)
    print(f"backup: {path}.bak-{stamp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
