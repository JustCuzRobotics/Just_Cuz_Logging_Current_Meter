#!/usr/bin/env python3
"""
Generate BOM.csv (JLCPCB upload format) and BOM.md from the schematic itself.

Reading the real .kicad_sch rather than a hand-kept table means the BOM cannot silently
drift away from the design.  Every designator, value and footprint below comes out of the
file; only the sourcing metadata (tier, stock, price, package, notes) lives here.
"""

import csv
import sys
from collections import defaultdict

sys.path.insert(0, ".")
from sexp import loads, find, findall

# --------------------------------------------------------------------------------------
# Sourcing data.  Every LCSC code was read off jlcpcb.com/parts on 2026-07-27.
#   lcsc -> (mfr_part, package, tier, stock, unit_price_usd)
# --------------------------------------------------------------------------------------
SOURCING = {
    "C17526":  ("0805W8F2401T5E",      "0805",            "Basic",    439194,   0.0036),
    "C17673":  ("0805W8F4701T5E",      "0805",            "Basic",   7616330,   0.0109),
    "C149504": ("0805W8F1003T5E",      "0805",            "Basic",   6358740,   0.0103),
    "C17414":  ("0805W8F1002T5E",      "0805",            "Basic",  15940626,   0.0109),
    "C17513":  ("0805W8F1001T5E",      "0805",            "Basic",   9329211,   0.0050),
    "C17408":  ("0805W8F1000T5E",      "0805",            "Basic",   8218245,   0.0103),
    "C17477":  ("0805W8F0000T5E",      "0805",            "Basic",   8472397,   0.0100),
    "C49678":  ("CC0805KRX7R9BB104",   "0805",            "Basic",  22583308,   0.0321),
    "C83170":  ("CC0805KRX7R9BB103",   "0805",            "Extended", 1163183,  0.0183),
    "C1713":   ("CL21A106KOQNNNE",     "0805",            "Extended", 2367318,  0.0820),
    "C970684": ("RVT1C101M0605",       "SMD D6.3x5.4mm",  "Extended",   35452,  0.0328),
    "C1017":   ("GZ2012D601TF",        "0805",            "Basic",     560191,  0.0286),
    "C12765":  ("LBAT54SLT1G",         "SOT-23",          "Extended",  230864,  0.0196),
    "C318884": ("TS-1187A-B-A-B",      "SMD 5.1x5.1mm",   "Basic",    1363977,  0.0204),
    "C2856800": ("FPC-05F-14PH20",     "FPC 14P 0.5mm",   "Extended",    3170,  0.1166),
    "C499454": ("ACS770KCB-150U-PFF-T", "CB-5 PFF",       "Extended",     336,  9.9813),
}

NOTES = {
    "C1017":   "500 mA / 300 mOhm is ample; this rail carries only the sensor's ~14 mA",
    "C12765":  "BAT54**S** — the SERIES variant, pin 3 the common node. BAT54/A/C are "
               "NOT substitutes: with GND on pin 1 and +3V3 on pin 2 only the series "
               "part forms a two-way rail clamp on V_PACK. Fit a BAT54C by mistake and "
               "the clamp does nothing",
    "C970684": "SMD aluminium electrolytic, 6.3 x 5.4 mm, 16 V",
    "C83170":  "no Basic 10 nF 0805 exists; this one has 1.1 M in stock",
    "C1713":   "no Basic 10 uF 0805 exists; Samsung, 2.3 M in stock",
    "C499454": "the expensive line. Only 336 in stock -- buy early. See note on mounting. "
               "Marked *not for new designs*: **ACS772KCB-150U-PFF-T is a drop-in "
               "replacement needing no board changes** -- same package, same 26.66 mV/A, "
               "same V_CC/10 quiescent, same ratiometric behaviour, same 4.7 kOhm / 10 nF "
               "load limits. See DESIGN.md 4.1.1",
    "C2856800": "bottom-contact, hinged lid. Verify contact side against your FPC cable.",
    "C318884": "5.1 x 5.1 mm SMD tactile, 1.5 mm actuator, 1.6 N",
}

# Parts a pick-and-place will not handle -- hand-soldered regardless of assembly service.
HAND = {
    "U2":  ("Waveshare RP2040-Zero", "23-pad castellated module",
            "Buy direct from Waveshare / Amazon. Not in the JLCPCB library."),
    "J1":  ("XT60PW-M", "THT, board-mount male", "Amazon / HobbyKing. High-current, hand-solder."),
    "J3":  ("XT60PW-F", "THT, board-mount female", "Amazon / HobbyKing."),
    "J2":  ("XT30PW-M", "THT, board-mount male", "Amazon / HobbyKing."),
    "J4":  ("XT30PW-F", "THT, board-mount female", "Amazon / HobbyKing."),
    "RV1": ("JIERR 3362P-1-104 — LCSC `C48997913`", "THT trimmer, 100k, top adjust",
            "100 kOhm, 0.5 W, 7 x 6.8 mm, ~$0.19 @ 5, 30,920 in stock. Footprint was the "
            "wrong 3362 variant (pads in a row); rebuilt from the vendor EasyEDA model as "
            "the 3362P triangle — pins 1/3 5.08 mm apart, pin 2 offset 2.54 mm. "
            "Run Update Footprints from Library, then re-route T_NODE and T_POT_TOP. "
            "See DESIGN.md §4.4."),
    "J5":  ("1x2 2.54 mm header", "THT", "NTC probe pads."),
    "J8":  ("1x3 2.54 mm header", "THT",
            "AM32 ESC servo header: signal / +5V (NOT connected) / GND."),
}

DNP = {"R15", "R16"}

# Board features rather than purchased parts.
NO_PART = {"JP1": "3-pad solder jumper — bare copper, nothing to place."}
NO_PART.update({f"TP{i}": "Probe pad — bare copper." for i in range(1, 28)})
NO_PART.update({f"J{i}": "Spare rail pad — plated hole, nothing to place." for i in range(9, 13)})
NO_PART["J13"] = "GP0 expansion pad — plated hole, nothing to place."
NO_PART.update({f"H{i}": "M3 mounting hole — unplated, mechanical only." for i in range(1, 9)})


def load(path):
    root = loads(open(path, encoding="utf-8").read())
    parts = {}
    for ss in findall(root, "symbol"):
        if find(ss, "lib_id") is None:
            continue
        p = {q[1]: q[2] for q in findall(ss, "property")}
        ref = p.get("Reference", "?")
        if ref.startswith("#"):
            continue
        parts[ref] = p
    return parts


def refsort(r):
    head = "".join(c for c in r if c.isalpha())
    tail = "".join(c for c in r if c.isdigit())
    return (head, int(tail or 0))


def main():
    sch = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    parts = load(sch)

    smt = defaultdict(list)     # lcsc -> [refs]
    meta = {}
    for ref, p in parts.items():
        code = p.get("LCSC")
        if not code:
            continue
        smt[code].append(ref)
        vals, fp = meta.get(code, (set(), ""))
        if not isinstance(vals, set):
            vals = {vals}
        if ref not in DNP:                      # DNP values ("10k DNP") are not the part
            vals.add(p.get("Value", ""))
        meta[code] = (vals, p.get("Footprint", ""))

    # Collapse the value set into one BOM comment. Where parts sharing an LCSC code have
    # different schematic values (SW1 "MODE" vs SW2 "ZERO/TARE"), those are FUNCTIONS,
    # not part descriptions -- fall back to the manufacturer part number.
    for code, (vals, fp) in list(meta.items()):
        meta[code] = (vals.pop() if len(vals) == 1 else SOURCING[code][0], fp)

    # ---------------- BOM.csv : JLCPCB upload format -----------------------------------
    with open("../BOM.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for code in sorted(smt, key=lambda c: refsort(sorted(smt[c], key=refsort)[0])):
            refs = sorted(smt[code], key=refsort)
            placed = [r for r in refs if r not in DNP]
            if not placed:
                continue
            val, fp = meta[code]
            w.writerow([val, ",".join(placed), fp, code])

    # ---------------- BOM.md ------------------------------------------------------------
    total_parts = 0.0
    lines = []
    lines.append("# Bill of Materials\n")
    lines.append("ESC Test Bench Mainboard, Rev A. Generated from "
                 "`Logging_Current_Meter.kicad_sch`.\n")
    lines.append("**All LCSC part numbers below were read off jlcpcb.com/parts on "
                 "2026-07-27.** None are from memory. Stock and library tier change "
                 "constantly — re-check both before you order.\n")

    lines.append("\n## 1. SMT parts — what the pick-and-place places\n")
    lines.append("| Ref | Qty | Value | Package | LCSC | MFR part | Tier | Stock | $ ea | $ line |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")

    basic = ext = 0
    for code in sorted(smt, key=lambda c: refsort(sorted(smt[c], key=refsort)[0])):
        refs = sorted(smt[code], key=refsort)
        placed = [r for r in refs if r not in DNP]
        if not placed:
            continue
        val, fp = meta[code]
        mfr, pkg, tier, stock, price = SOURCING[code]
        line = price * len(placed)
        total_parts += line
        if tier == "Basic":
            basic += 1
        else:
            ext += 1
        lines.append(f"| {', '.join(placed)} | {len(placed)} | {val} | {pkg} | "
                     f"`{code}` | {mfr} | {tier} | {stock:,} | {price:.4f} | {line:.4f} |")

    lines.append(f"\n**{basic} Basic + {ext} Extended unique parts.** "
                 f"Parts cost ≈ **${total_parts:.2f}/board**, of which "
                 f"${SOURCING['C499454'][4]:.2f} is the ACS770 alone — everything else "
                 f"together is about ${total_parts - SOURCING['C499454'][4]:.2f}.\n")
    lines.append("JLCPCB charges a one-off feeder setup fee (roughly $3) per unique "
                 "**Extended** part; Basic parts are free. With "
                 f"{ext} Extended lines that is about ${ext * 3} on the first order.\n")

    lines.append("\n### Notes on specific lines\n")
    for code, note in NOTES.items():
        lines.append(f"- **`{code}`** ({SOURCING[code][0]}) — {note}")

    lines.append("\n\n## 2. Not fitted\n")
    lines.append("| Ref | Value | Why |")
    lines.append("|---|---|---|")
    lines.append("| R15, R16 | 10k | Button pull-ups. The RP2040's internal pull-ups are "
                 "used; these are footprint-only fallbacks. |")

    lines.append("\n\n## 3. Hand-soldered — not placeable\n")
    lines.append("| Ref | Part | Package | Note |")
    lines.append("|---|---|---|---|")
    for ref in sorted(HAND, key=refsort):
        mfr, pkg, note = HAND[ref]
        lines.append(f"| {ref} | {mfr} | {pkg} | {note} |")
    lines.append("\nPlus, off-board: the Hosyond 3.5\" ST7796U display module and a "
                 "100k B3950 NTC probe.\n")

    lines.append("\n## 4. Board features, not parts\n")
    lines.append("| Ref | Note |")
    lines.append("|---|---|")
    groups = {}
    for ref, note in NO_PART.items():
        groups.setdefault(note, []).append(ref)
    for note, refs in sorted(groups.items()):
        refs = sorted(refs, key=refsort)
        span = refs[0] if len(refs) == 1 else f"{refs[0]}\u2013{refs[-1]} ({len(refs)})"
        lines.append(f"| {span} | {note} |")

    open("../BOM.md", "w", encoding="utf-8").write("\n".join(lines) + "\n")

    print(f"wrote ../BOM.csv  ({sum(1 for c in smt)} SMT lines)")
    print(f"wrote ../BOM.md")
    print(f"parts cost/board ≈ ${total_parts:.2f}   Basic={basic} Extended={ext}")

    missing = [r for r in parts
               if r not in HAND and r not in NO_PART and not parts[r].get("LCSC")]
    if missing:
        print("no LCSC and not listed as hand-soldered:", sorted(missing, key=refsort))
    return 0


if __name__ == "__main__":
    sys.exit(main())
