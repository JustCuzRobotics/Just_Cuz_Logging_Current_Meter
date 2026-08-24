#!/usr/bin/env python3
"""
Rework the ESC connector: 4-pin with telemetry -> 3-pin AM32 servo header.

What changes and why
--------------------
J8 was a 1x04 carrying signal, telemetry, +5V and GND.  AM32 ESCs present a standard
3-wire servo lead -- **signal, +5V, GND, in that order** -- and the telemetry channel was
never likely to be used, so it goes.

  * `J8` becomes `LCM:CONN_1x3`: pin 1 `ESC_SIG`, pin 2 **no-connect**, pin 3 `GND`.
  * `R18` (1k telemetry series) and `D2` (BAT54S clamp) are removed with it.
  * Nets `ESC_TELEM` and `ESC_TELEM_MCU` disappear.
  * **GP0 is freed.**  U2 pin 10's stub is relabelled `GP0` and brought out to `J13`, a
    plated rail pad, so the pin stays reachable.  Section 3's binding constraint was that
    there were no spare pins; this is the first one.

**Pin 2 is deliberately not connected.**  The RP2040-Zero ties VSYS straight to VBUS, so
wiring an ESC's BEC to +5V here would back-feed the USB host whenever both were plugged
in.  The pad exists only so the servo plug seats squarely.  A `no_connect` marker is
placed on it, which is also what stops KiCad's ERC reporting it as a floating pin.

J8 is replaced rather than edited: a 1x03 has different pin geometry from a 1x04, so the
old stubs would not line up.  It is deleted with `strip_sch.remove_symbols` (which finds
its stubs from the library pin offsets) and rebuilt at the same sheet origin.

Usage:  python3 patch_esc.py ../Logging_Current_Meter.kicad_sch ../LCM.kicad_sym
"""

import datetime
import re
import shutil
import sys
import uuid as _uuid

sys.path.insert(0, ".")
from strip_sch import remove_symbols, find_placed, span

TAB = "\t"
STUB = 10.16           # J8's original stub length, kept
RAIL_STUB = 5.08       # block K convention


def U():
    return str(_uuid.uuid4())


# ======================================================================================
#  The CONN_1x3 library symbol -- modelled on CONN_1x4, one pin shorter
# ======================================================================================
def conn_1x3(indent):
    t = indent
    pins = "".join(f"""{t}  (pin passive line
{t}    (at -5.08 {2.54 - 2.54 * i} 0)
{t}    (length 3.81)
{t}    (name "{i + 1}"
{t}      (effects
{t}        (font
{t}          (size 1.27 1.27)
{t}        )
{t}      )
{t}    )
{t}    (number "{i + 1}"
{t}      (effects
{t}        (font
{t}          (size 1.27 1.27)
{t}        )
{t}      )
{t}    )
{t}  )
""" for i in range(3))

    return f"""{t}(symbol "CONN_1x3"
{t}  (pin_names
{t}    (offset 1.016)
{t}  )
{t}  (exclude_from_sim no)
{t}  (in_bom yes)
{t}  (on_board yes)
{t}  (property "Reference" "J"
{t}    (at 0 5.08 0)
{t}    (effects
{t}      (font
{t}        (size 1.27 1.27)
{t}      )
{t}      (justify left)
{t}    )
{t}  )
{t}  (property "Value" "CONN_1x3"
{t}    (at 0 -5.08 0)
{t}    (effects
{t}      (font
{t}        (size 1.27 1.27)
{t}      )
{t}      (justify left)
{t}    )
{t}  )
{t}  (property "Footprint" ""
{t}    (at 0 -7.62 0)
{t}    (effects
{t}      (font
{t}        (size 1.27 1.27)
{t}      )
{t}      (justify left) hide
{t}    )
{t}  )
{t}  (property "Datasheet" "~"
{t}    (at 0 -10.16 0)
{t}    (effects
{t}      (font
{t}        (size 1.27 1.27)
{t}      )
{t}      (justify left) hide
{t}    )
{t}  )
{t}  (property "Description" "3-pin header"
{t}    (at 0 -12.7 0)
{t}    (effects
{t}      (font
{t}        (size 1.27 1.27)
{t}      )
{t}      (justify left) hide
{t}    )
{t}  )
{t}  (symbol "CONN_1x3_0_1"
{t}    (rectangle
{t}      (start -1.27 -3.81)
{t}      (end 2.54 3.81)
{t}      (stroke
{t}        (width 0.254)
{t}        (type default)
{t}      )
{t}      (fill
{t}        (type none)
{t}      )
{t}    )
{t}  )
{t}  (symbol "CONN_1x3_1_1"
{pins}{t}  )
{t})
"""


# ======================================================================================
#  Schematic item templates
# ======================================================================================
def sym_instance(lib_id, ref, value, footprint, x, y, root_uuid, in_bom="yes"):
    t = TAB
    return f"""{t}(symbol
{t}{t}(lib_id "{lib_id}")
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
{t}{t}{t}(at {x + 2.54} {y - 5.08} 0)
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
{t}{t}{t}(at {x + 2.54} {y + 6.35} 0)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t}{t}(justify left)
{t}{t}{t})
{t}{t})
{t}{t}(property "Footprint" "{footprint}"
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
{t}{t}(property "Datasheet" ""
{t}{t}{t}(at {x} {y + 11.43} 0)
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
{t}{t}{t}(hide yes)
{t}{t}{t}(show_name no)
{t}{t}{t}(do_not_autoplace no)
{t}{t}{t}(effects
{t}{t}{t}{t}(font
{t}{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{{PINS}}{t}{t}(instances
{t}{t}{t}(project "Logging_Current_Meter"
{t}{t}{t}{t}(path "/{root_uuid}"
{t}{t}{t}{t}{t}(reference "{ref}")
{t}{t}{t}{t}{t}(unit 1)
{t}{t}{t}{t})
{t}{t}{t})
{t}{t})
{t})
"""


PINS_PLACEHOLDER = "{PINS}"


def pin_block(n):
    t = TAB
    return "".join(f"""{t}{t}(pin "{i}"
{t}{t}{t}(uuid "{U()}")
{t}{t})
""" for i in range(1, n + 1))


def wire(x1, y1, x2, y2):
    t = TAB
    return f"""{t}(wire
{t}{t}(pts
{t}{t}{t}(xy {x1:g} {y1:g}) (xy {x2:g} {y2:g})
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
{t}{t}(at {x:g} {y:g} 0)
{t}{t}(effects
{t}{t}{t}(font
{t}{t}{t}{t}(size 1.27 1.27)
{t}{t}{t})
{t}{t}{t}(justify left bottom)
{t}{t})
{t}{t}(uuid "{U()}")
{t})
"""


def no_connect(x, y):
    t = TAB
    return f"""{t}(no_connect
{t}{t}(at {x:g} {y:g})
{t}{t}(uuid "{U()}")
{t})
"""


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    libpath = sys.argv[2] if len(sys.argv) > 2 else "../LCM.kicad_sym"
    dry = "--dry-run" in sys.argv

    text = open(path, encoding="utf-8").read()
    lib = open(libpath, encoding="utf-8").read()

    if '"LCM:CONN_1x3"' in text:
        print("already patched; nothing to do")
        return 0

    placed = find_placed(text)
    if "J8" not in placed:
        raise SystemExit("J8 not found")
    _, _, jx, jy, jrot, jlib = placed["J8"]
    print(f"J8 at ({jx}, {jy}) rot {jrot}, currently {jlib}")

    root_uuid = re.search(r'\(uuid "([0-9a-f-]{36})"\)', text).group(1)

    # ---- 1. CONN_1x3 into both libraries ----------------------------------------------
    i = lib.index("(symbol \"CONN_1x4\"")
    # indent used inside LCM.kicad_sym
    line_start = lib.rindex("\n", 0, i) + 1
    lib_indent = lib[line_start:i]
    lib = lib[:line_start] + conn_1x3(lib_indent) + lib[line_start:]

    j = text.index('(symbol "LCM:CONN_1x4"')
    line_start = text.rindex("\n", 0, j) + 1
    sch_indent = text[line_start:j]
    entry = conn_1x3(sch_indent).replace('(symbol "CONN_1x3"',
                                         '(symbol "LCM:CONN_1x3"', 1)
    entry = entry.replace('"CONN_1x3_0_1"', '"CONN_1x3_0_1"')
    text = text[:line_start] + entry + text[line_start:]
    print("added CONN_1x3 to LCM.kicad_sym and the schematic's lib_symbols")

    # ---- 2. remove the old J8, R18, D2 -------------------------------------------------
    text = remove_symbols(text, ["J8", "R18", "D2"])

    # ---- 3. rebuild J8 as a 3-pin header ----------------------------------------------
    # Pin k sits at local (-5.08, 2.54 - 2.54k); sheet Y is inverted.
    px = jx - 5.08
    ys = [jy - 2.54, jy, jy + 2.54]

    # LCM:, not Connector_PinHeader_2.54mm:.  The stock KiCad footprint lives in the
    # KiCad installation and cannot be synthesised by the generator, so the 1x03 was
    # derived from the real 1x04 already on the board -- KiCad's own pad geometry, just
    # one pin shorter -- and saved into LCM.pretty.  Same self-contained rule as the
    # symbols in section 12.
    body = sym_instance("LCM:CONN_1x3", "J8", "ESC 3-pin",
                        "LCM:PinHeader_1x03_P2.54mm_Vertical",
                        jx, jy, root_uuid).replace(PINS_PLACEHOLDER, pin_block(3))

    items = [body,
             wire(px, ys[0], px - STUB, ys[0]), label("ESC_SIG", px - STUB, ys[0]),
             no_connect(px, ys[1]),
             wire(px, ys[2], px - STUB, ys[2]), label("GND", px - STUB, ys[2])]
    print(f"  J8.1 ESC_SIG @ y={ys[0]}   J8.2 no-connect @ y={ys[1]}   "
          f"J8.3 GND @ y={ys[2]}")

    # ---- 4. GP0: relabel U2's stub, and bring it out to J13 ----------------------------
    n = text.count('(label "ESC_TELEM_MCU"')
    if n != 1:
        raise SystemExit(f"expected exactly 1 ESC_TELEM_MCU label left, found {n}")
    text = text.replace('(label "ESC_TELEM_MCU"', '(label "GP0"')
    print("  relabelled U2 pin 10 stub: ESC_TELEM_MCU -> GP0")

    # J13 goes in block K, in the slot after the four rail pads.  Grid origin is read
    # from TP1 rather than assumed -- the block has been dragged in KiCad.
    if "TP1" not in placed:
        raise SystemExit("TP1 missing; cannot locate the block K grid")
    # Slots 0-9 are TP1-TP10 and 10-13 are the rail pads J9-J12, so 14 is the first
    # free one: row 2, column 0.  That is one row below where strip_sch.py shrank the
    # block K rectangle to, so the rectangle grows back by a row further down.
    x0, y0 = placed["TP1"][2], placed["TP1"][3]
    SLOT = 14
    jx13 = x0 + (SLOT % 7) * 25.4
    jy13 = y0 + (SLOT // 7) * 8.89
    items += [sym_instance("LCM:TestPoint", "J13", "GP0",
                           "LCM:RailPad_THT_D1.0mm", jx13, jy13,
                           root_uuid, in_bom="no").replace(PINS_PLACEHOLDER,
                                                           pin_block(1)),
              wire(jx13, jy13, jx13 + RAIL_STUB, jy13),
              label("GP0", jx13 + RAIL_STUB, jy13)]
    print(f"  J13 (GP0 rail pad) @ ({jx13:.2f}, {jy13:.2f})")

    # grow block K back to cover the row J13 now sits in
    want_bottom = jy13 + 6.35
    for m in re.finditer(r"\(rectangle\n\t\t\(start ([-\d.]+) ([-\d.]+)\)\n\t\t"
                         r"\(end ([-\d.]+) ([-\d.]+)\)", text):
        x1, y1, x2, y2 = (float(g) for g in m.groups())
        if not (min(x1, x2) <= x0 <= max(x1, x2) and min(y1, y2) <= y0 <= max(y1, y2)):
            continue
        if max(y1, y2) >= want_bottom:
            break
        text = (text[:m.start()]
                + f"(rectangle\n\t\t(start {x1:g} {y1:g})\n\t\t"
                  f"(end {x2:g} {want_bottom:g})"
                + text[m.end():])
        print(f"  block K rectangle: bottom {max(y1, y2):.2f} -> {want_bottom:.2f}")
        break

    k = text.index("\t(sheet_instances")
    text = text[:k] + "".join(items) + text[k:]

    if dry:
        print("\n-- dry run, nothing written --")
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copy2(path, f"{path}.bak-{stamp}")
    shutil.copy2(libpath, f"{libpath}.bak-{stamp}")
    open(path, "w", encoding="utf-8").write(text)
    open(libpath, "w", encoding="utf-8").write(lib)
    print(f"\nwrote {path}\nwrote {libpath}")
    print("\nNow run:  python3 verify.py ../Logging_Current_Meter.kicad_sch ../LCM.kicad_sym")
    return 0


if __name__ == "__main__":
    sys.exit(main())
