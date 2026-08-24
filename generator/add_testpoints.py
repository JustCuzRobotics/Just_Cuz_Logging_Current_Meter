#!/usr/bin/env python3
"""
Add test points and spare rail pads to the hand-edited schematic.

Same rule as patch_sch.py: the file has been edited in KiCad 10, so it is modified as
TEXT and only by insertion. Nothing existing is rewritten, so every symbol position,
wire, label and uuid is untouched.

Inserted per test point: one LCM:TestPoint symbol, one wire stub, one net label. The
label is what actually creates the connection -- same "signal name" convention the rest
of the sheet uses -- so the test points join existing nets without touching those nets'
existing geometry.

Written in KiCad 10 syntax (body_style / in_pos_files / show_name / (hide yes)),
templated from real symbols already in this file.
"""

import re
import shutil
import sys
import uuid as _uuid
import datetime

sys.path.insert(0, ".")
from sexp import loads, find, findall

TAB = "\t"

# Nets worth a probe, in placement order. Analog first (calibration and bring-up),
# then the display bus, then user IO.
SIGNAL_TPS = [
    "PACK+", "LOAD+", "+5VS", "I_RAW", "I_SENSE", "V5_SENSE", "VPACK_TAP", "V_PACK",
    "T_NODE", "T_SENSE", "DISP_VCC", "SPI_SCK", "SPI_MOSI", "SPI_MISO", "LCD_CS",
    "LCD_RS", "LCD_RST", "SD_CS", "CTP_SDA", "CTP_SCL", "CTP_INT", "CTP_RST",
    "DISP_LED", "BTN1", "BTN2", "ESC_SIG", "ESC_TELEM",
]
# Spare rail pads -- bigger, drilled, so a wire can be soldered to them.
RAIL_PADS = ["+3V3", "+5V", "GND", "GND"]

FP_SIGNAL = "LCM:TestPoint_D1.5mm"
FP_RAIL = "LCM:RailPad_THT_D1.0mm"

# Layout: the free strip below block I. Everything on a 1.27 mm grid.
X0, Y0, XP, YP, COLS = 396.24, 370.84, 25.4, 8.89, 7
BLOCK = (391.16, 358.14, 576.58, 409.0)
STUB = 5.08


def U():
    return str(_uuid.uuid4())


def sym_instance(ref, value, footprint, x, y, root_uuid, in_bom="no"):
    t = TAB
    return f"""{t}(symbol
{t}{t}(lib_id "LCM:TestPoint")
{t}{t}(at {x} {y} 0)
{t}{t}(unit 1)
{t}{t}(body_style 1)
{t}{t}(exclude_from_sim no)
{t}{t}(in_bom {in_bom})
{t}{t}(on_board yes)
{t}{t}(in_pos_files no)
{t}{t}(dnp no)
{t}{t}(uuid "{U()}")
{t}{t}(property "Reference" "{ref}"
{t}{t}{t}(at {x} {y - 3.81} 0)
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
{t}{t}{t}(at {x} {y + 3.81} 0)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t}{t}(hide yes)
{t}{t}{t})
{t}{t})
{t}{t}(property "Footprint" "{footprint}"
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
{t}{t}(property "Description" ""
{t}{t}{t}(at {x} {y} 0)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{t}{t}(pin "1"
{t}{t}{t}(uuid "{U()}")
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


def wire(x1, y1, x2, y2):
    t = TAB
    return f"""{t}(wire
{t}{t}(pts
{t}{t}{t}(xy {x1} {y1}) (xy {x2} {y2})
{t}{t})
{t}{t}(stroke
{t}{t}{t}(width 0)
{t}{t}{t}(type default)
{t}{t})
{t}{t}(uuid "{U()}")
{t})
"""


def label(text, x, y):
    t = TAB
    return f"""{t}(label "{text}"
{t}{t}(at {x} {y} 0)
{t}{t}(effects
{t}{t}{t}(font
{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t})
{t}{t}{t}(justify left bottom)
{t}{t})
{t}{t}(uuid "{U()}")
{t})
"""


def rect_and_title(title, x1, y1, x2, y2):
    t = TAB
    return f"""{t}(rectangle
{t}{t}(start {x1} {y1})
{t}{t}(end {x2} {y2})
{t}{t}(stroke
{t}{t}{t}(width 0.254)
{t}{t}{t}(type dash)
{t}{t})
{t}{t}(fill
{t}{t}{t}(type none)
{t}{t})
{t}{t}(uuid "{U()}")
{t})
{t}(text "{title}"
{t}{t}(at {x1 + 2.54} {y1 + 5.08} 0)
{t}{t}(effects
{t}{t}{t}(font
{t}{t}{t}{t}(size 2.54 2.54)
{t}{t}{t}{t}(bold yes)
{t}{t}{t})
{t}{t}{t}(justify left)
{t}{t})
{t}{t}(uuid "{U()}")
{t})
"""


# KiCad 10 lib_symbols entry for the test point itself.
LIB_SYMBOL = f"""{TAB}{TAB}(symbol "LCM:TestPoint"
{TAB}{TAB}{TAB}(pin_numbers
{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(pin_names
{TAB}{TAB}{TAB}{TAB}(offset 0.254)
{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(exclude_from_sim no)
{TAB}{TAB}{TAB}(in_bom no)
{TAB}{TAB}{TAB}(on_board yes)
{TAB}{TAB}{TAB}(in_pos_files no)
{TAB}{TAB}{TAB}(duplicate_pin_numbers_are_jumpers no)
{TAB}{TAB}{TAB}(property "Reference" "TP"
{TAB}{TAB}{TAB}{TAB}(at 0 3.81 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(property "Value" "TestPoint"
{TAB}{TAB}{TAB}{TAB}(at 0 -3.81 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(hide yes)
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
{TAB}{TAB}{TAB}(property "Description" "Probe pad"
{TAB}{TAB}{TAB}{TAB}(at 0 0 0)
{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(hide yes)
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(symbol "TestPoint_0_1"
{TAB}{TAB}{TAB}{TAB}(circle
{TAB}{TAB}{TAB}{TAB}{TAB}(center -3.175 0)
{TAB}{TAB}{TAB}{TAB}{TAB}(radius 0.635)
{TAB}{TAB}{TAB}{TAB}{TAB}(stroke
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(width 0.254)
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type default)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(fill
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(type none)
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}(symbol "TestPoint_1_1"
{TAB}{TAB}{TAB}{TAB}(pin passive line
{TAB}{TAB}{TAB}{TAB}{TAB}(at 0 0 180)
{TAB}{TAB}{TAB}{TAB}{TAB}(length 2.54)
{TAB}{TAB}{TAB}{TAB}{TAB}(name "1"
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}(number "1"
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(effects
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(font
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}(size 1.27 1.27)
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB}{TAB})
{TAB}{TAB}{TAB})
{TAB}{TAB})
"""


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    text = open(path, encoding="utf-8").read()

    if '"LCM:TestPoint"' in text:
        print("test points already present; nothing to do")
        return 0

    root_uuid = re.search(r'\(uuid "([0-9a-f-]{36})"\)', text).group(1)
    print("root sheet uuid:", root_uuid)

    # ---- insert the lib_symbol just after (lib_symbols -------------------------------
    i = text.index("(lib_symbols")
    j = text.index("\n", i) + 1
    text = text[:j] + LIB_SYMBOL + text[j:]

    # ---- build the new items ----------------------------------------------------------
    items = [rect_and_title("K  TEST POINTS + SPARE RAIL PADS", *BLOCK)]
    plan = ([(n, FP_SIGNAL, "TP") for n in SIGNAL_TPS]
            + [(n, FP_RAIL, "J") for n in RAIL_PADS])

    tp_i, j_i = 0, 9
    placed = []
    for k, (net, fp, kind) in enumerate(plan):
        col, row = k % COLS, k // COLS
        x = X0 + col * XP
        y = Y0 + row * YP
        if kind == "TP":
            tp_i += 1
            ref = f"TP{tp_i}"
        else:
            ref = f"J{j_i}"
            j_i += 1
        items.append(sym_instance(ref, net, fp, x, y, root_uuid))
        items.append(wire(x, y, x + STUB, y))
        items.append(label(net, x + STUB, y))
        placed.append((ref, net, fp, x, y))

    # ---- insert before (sheet_instances ------------------------------------------------
    k = text.index("\t(sheet_instances")
    text = text[:k] + "".join(items) + text[k:]

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copy2(path, f"{path}.bak-{stamp}")
    open(path, "w", encoding="utf-8").write(text)

    print(f"added {len(placed)} items ({tp_i} test points, {j_i - 9} rail pads)")
    for ref, net, fp, x, y in placed:
        print(f"   {ref:5s} {net:14s} @({x:7.2f},{y:7.2f})  {fp.split(':')[-1]}")
    print(f"backup: {path}.bak-{stamp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
