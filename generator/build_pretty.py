#!/usr/bin/env python3
"""
Build LCM.pretty from the vendor footprints in parts-dump-pcb/ and easyECADDownloader/.

Vendor footprints are not used as-is. Each one gets its pad names remapped so they match
the pin NUMBERS of the symbol already used in the schematic — otherwise a pad whose name
doesn't correspond to any symbol pin simply carries no net, silently.

Every remap below is justified in a comment. After writing, the script re-reads each
finished footprint and asserts its pad set covers the symbol's pin set.
"""

import os
import re
import shutil
import sys

sys.path.insert(0, ".")
from sexp import loads, dumps, find, findall, Sym

SRC_UL = "../parts-dump-pcb"
SRC_EE = "../easyECADDownloader/easyECADDownloader.pretty"
OUT = "../LCM.pretty"

# RP2040-Zero: the vendor numbers pads 1..23 as a continuous ring starting at GP0.
# This project's symbol numbers them left column, then right column, then bottom.
# Mapping is therefore done by SIGNAL NAME, which is the only thing both agree on.
ZERO_VENDOR = {  # vendor pad number -> signal name (read from RP2040-ZERO.kicad_sym)
    1: "GP0", 2: "GP1", 3: "GP2", 4: "GP3", 5: "GP4", 6: "GP5", 7: "GP6", 8: "GP7",
    9: "GP8", 10: "GP9", 11: "GP10", 12: "GP11", 13: "GP12", 14: "GP13", 15: "GP14",
    16: "GP15", 17: "GP26", 18: "GP27", 19: "GP28", 20: "GP29", 21: "3V3", 22: "GND",
    23: "5V",
}
ZERO_LEFT = ["5V", "GND", "3V3", "GP29", "GP28", "GP27", "GP26", "GP15", "GP14"]
ZERO_RIGHT = ["GP0", "GP1", "GP2", "GP3", "GP4", "GP5", "GP6", "GP7", "GP8"]
ZERO_BOTTOM = ["GP9", "GP10", "GP11", "GP12", "GP13"]
ZERO_MINE = {n: i + 1 for i, n in enumerate(ZERO_LEFT + ZERO_RIGHT + ZERO_BOTTOM)}
ZERO_REMAP = {str(v): str(ZERO_MINE[name]) for v, name in ZERO_VENDOR.items()}

JOBS = [
    # (output name, source file, pad remap, note)
    ("ACS770_CB_PFF", f"{SRC_UL}/ACS770KCB-150U-PFF-T (1)/XDCR_ACS770KCB-150U-PFF-T.kicad_mod",
     "acs770",
     "5 terminals + 32 perimeter stitching vias. The 4_n / 5_n vias are collapsed onto "
     "pads 4 and 5 so they actually join the IP+ / IP- nets."),

    ("RP2040_Zero_Castellated", f"{SRC_UL}/RP2040-ZERO/MODULE_RP2040-ZERO.kicad_mod",
     ZERO_REMAP,
     "Renumbered from the vendor's ring order to this project's symbol order, by signal name."),

    ("XT60PW-M", f"{SRC_UL}/XT60PW-M/AMASS_XT60PW-M.kicad_mod", {},
     "Pads 1/2 already match the symbol (pin 1 = +). SH1/SH2 are housing posts."),

    ("XT60PW-F", f"{SRC_UL}/XT60PW-F (1)/AMASS_XT60PW-F.kicad_mod", {},
     "As above, mirrored for the mating half."),

    ("XT30PW-M", f"{SRC_UL}/XT30PW-M (1)/AMASS_XT30PW-M.kicad_mod", {"P": "1", "N": "2"},
     "Vendor names the pads P/N; the symbol uses 1/2. P is positive, so P->1, N->2."),

    ("XT30PW-F", f"{SRC_UL}/XT30PW-F (1)/AMASS_XT30PW-F.kicad_mod", {"P": "1", "N": "2"},
     "As above."),

    ("FPC_05F_14PH20_P0.5mm", f"{SRC_EE}/FPC-SMD_14P-P0.50_XUNPU_FPC-05F-14PH20.kicad_mod",
     {},
     "Pads 1-14 already match DISPLAY_14. 15/16 are mechanical hold-downs, no net."),

    ("SW_TS-1187A_5.1x5.1mm", f"{SRC_EE}/SW-SMD_4P-L5.1-W5.1-P3.70-LS6.5-TL_H1.5.kicad_mod",
     {"1": "1", "3": "1", "2": "2", "4": "2"},
     "4-pad tactile: pads 1,3 are the left leadframe and 2,4 the right; each side is "
     "internally common, so they collapse to the symbol's two pins."),
]

# What the schematic's symbols expect, for the final cross-check.
EXPECT_PINS = {
    "ACS770_CB_PFF": {"1", "2", "3", "4", "5"},
    "RP2040_Zero_Castellated": {str(i) for i in range(1, 24)},
    "XT60PW-M": {"1", "2"}, "XT60PW-F": {"1", "2"},
    "XT30PW-M": {"1", "2"}, "XT30PW-F": {"1", "2"},
    "FPC_05F_14PH20_P0.5mm": {str(i) for i in range(1, 15)},
    "SW_TS-1187A_5.1x5.1mm": {"1", "2"},
}


def remap_pads(root, mapping):
    """Rename pad identifiers. `mapping` is a dict, or 'acs770' for the regex rule."""
    changed = 0
    for p in findall(root, "pad"):
        old = str(p[1])
        if mapping == "acs770":
            m = re.match(r"^([45])_\d+$", old)
            new = m.group(1) if m else old
        else:
            new = mapping.get(old, old)
        if new != old:
            p[1] = Sym(new) if new.isdigit() else new
            changed += 1
    return changed


def main():
    os.makedirs(OUT, exist_ok=True)
    report = []

    for name, src, mapping, note in JOBS:
        if not os.path.exists(src):
            report.append((name, "MISSING SOURCE", src, 0, set()))
            continue
        txt = open(src, encoding="utf-8", errors="replace").read()
        root = loads(txt)

        # Round-trip safety: if my writer cannot reproduce the parse tree, do not touch it.
        if loads(dumps(root)) != root:
            report.append((name, "ROUND-TRIP FAILED", src, 0, set()))
            continue

        n = remap_pads(root, mapping)

        root[1] = name                                   # footprint name
        for t in findall(root, "fp_text"):
            if str(t[1]) == "value":
                t[2] = name
        # descriptive tags
        root.append([Sym("descr"), note])

        out = os.path.join(OUT, f"{name}.kicad_mod")
        open(out, "w", encoding="utf-8").write(dumps(root) + "\n")

        pads = {str(p[1]) for p in findall(root, "pad")}
        report.append((name, "ok", f"{n} pads renamed", len(findall(root, "pad")), pads))

    print(f"{'footprint':26s} {'status':18s} {'remap':16s} pads")
    print("-" * 78)
    for name, status, detail, npads, pads in report:
        print(f"{name:26s} {status:18s} {str(detail)[:16]:16s} {npads}")

    print("\n--- cross-check against symbol pin numbers ---")
    bad = 0
    for name, status, detail, npads, pads in report:
        if status != "ok":
            bad += 1
            continue
        want = EXPECT_PINS.get(name, set())
        missing = want - pads
        extra = pads - want
        mech = {p for p in extra if not p.isdigit() or int(p) > 100}
        extra -= mech
        flag = "OK " if not missing else "FAIL"
        if missing:
            bad += 1
        print(f"  {flag} {name:26s} covers {len(want & pads)}/{len(want)} symbol pins"
              + (f"   MISSING {sorted(missing)}" if missing else "")
              + (f"   mechanical-only: {sorted(mech)}" if mech else "")
              + (f"   unused: {sorted(extra)}" if extra else ""))
    print(f"\n{'ALL FOOTPRINTS OK' if not bad else str(bad) + ' PROBLEM(S)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
