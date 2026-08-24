#!/usr/bin/env python3
"""
Surgical patcher for Logging_Current_Meter.kicad_sch.

The schematic has been opened and edited in KiCad 10, so it must NEVER be regenerated
from gen_sch.py -- that would discard hand placement.  This script edits the file as
TEXT, touching only the specific property values it is told to change, so every symbol
position, every wire, every uuid and KiCad's own formatting survive untouched.

It works by locating each placed symbol's paren-balanced span, then rewriting only the
quoted value inside the named property blocks within that span.

Usage:  python3 patch_sch.py ../Logging_Current_Meter.kicad_sch
        python3 patch_sch.py ../Logging_Current_Meter.kicad_sch --dry-run
"""

import re
import shutil
import sys
import datetime

# ======================================================================================
#  What to change
# ======================================================================================

# Value changes: moving off E96 (Extended, thin stock) onto E24 (JLCPCB Basic, millions
# in stock).  Ratios recomputed and verified -- see BOM.md section "Value changes".
VALUE_CHANGES = {
    "R1": "2.4k 1%",     # was 2k49 -- with R2 gives 0.66197, 150 A -> 2.978 V
    "R2": "4.7k 1%",     # was 4k99
    "R3": "100k 1%",     # was 95k3 -- with R4/R6 gives 21:1, 60 V -> 2.857 V
    "R4": "100k 1%",     # was 95k3
}

# Footprint assignments.
#   LCM: prefixed  -> custom footprint, still to be built (see BOM.md)
#   everything else -> stock KiCad 8/9/10 library footprint
FOOTPRINTS = {
    # passives
    **{r: "Resistor_SMD:R_0805_2012Metric" for r in
       ["R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10",
        "R11", "R12", "R13", "R14", "R15", "R16", "R17", "R18"]},
    **{c: "Capacitor_SMD:C_0805_2012Metric" for c in
       ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9",
        "C11", "C12", "C13"]},
    "C10": "Capacitor_SMD:CP_Elec_6.3x5.4",
    "FB1": "Inductor_SMD:L_0805_2012Metric",
    "D1": "Package_TO_SOT_SMD:SOT-23",
    "D2": "Package_TO_SOT_SMD:SOT-23",
    # switches -- converted to SMD (TS-1187A-B-A-B, 5.1 x 5.1 mm, JLCPCB Basic)
    "SW1": "LCM:SW_TS-1187A_5.1x5.1mm",
    "SW2": "LCM:SW_TS-1187A_5.1x5.1mm",
    # hand-soldered
    "RV1": "LCM:Potentiometer_Bourns_3362P_Vertical",
    "J5": "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
    "J6": "Connector_PinHeader_2.54mm:PinHeader_1x14_P2.54mm_Vertical",
    "J8": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "JP1": "LCM:SolderJumper_3_P1.3mm_Open",
    # LCM.pretty, built by build_pretty.py from vendor sources with pads remapped
    "U1": "LCM:ACS770_CB_PFF",
    "U2": "LCM:RP2040_Zero_Castellated",
    # Input mates with the battery (which carries the female half), so the board's input
    # is MALE; output mates with the ESC (male), so the board's output is FEMALE.
    # M and F are NOT the same footprint -- the pads are mirrored.
    "J1": "LCM:XT60PW-M",
    "J3": "LCM:XT60PW-F",
    "J2": "LCM:XT30PW-M",
    "J4": "LCM:XT30PW-F",
    "J7": "LCM:FPC_05F_14PH20_P0.5mm",
}

# LCSC / JLCPCB part numbers.  Every one of these was read off jlcpcb.com/parts on
# 2026-07-27; none are from memory.  Tier and stock are recorded in BOM.md.
LCSC = {
    **{r: "C17414" for r in ["R5", "R6", "R8", "R15", "R16"]},          # 10k 1% Basic
    "R1": "C17526",                                                     # 2.4k 1% Basic
    "R2": "C17673",                                                     # 4.7k 1% Basic
    "R3": "C149504", "R4": "C149504",                                   # 100k 1% Basic
    **{r: "C17513" for r in ["R7", "R9", "R18"]},                       # 1k 1% Basic
    "R17": "C17408",                                                    # 100R 1% Basic
    **{r: "C17477" for r in ["R10", "R11", "R12", "R13", "R14"]},       # 0R Basic
    **{c: "C49678" for c in ["C1", "C5", "C6", "C7", "C9", "C11", "C12", "C13"]},  # 100n
    "C3": "C83170", "C4": "C83170",                                     # 10n
    "C2": "C1713", "C8": "C1713",                                       # 10u 16V
    "C10": "C970684",                                                   # 100u 16V SMD
    "FB1": "C1017",                                                     # ferrite Basic
    "D1": "C12765", "D2": "C12765",                                     # BAT54S
    "SW1": "C318884", "SW2": "C318884",                                 # tactile Basic
    "J7": "C2856800",                                                   # FPC 14P
    "U1": "C499454",                                                    # ACS770
}

# Free-text notes on the sheet that quote the old values.
TEXT_CHANGES = [
    ("R1/R2 scale 0.5-4.5 V down to 0.33-3.00 V.",
     "R1/R2 scale 0.5-4.5 V down to 0.33-2.98 V."),
    ("Two 95k3 in series keeps each part inside its",
     "Two 100k in series keeps each part inside its"),
    ("60.0 V in -> 2.99 V at GP27. Divider drains 299 uA",
     "60.0 V in -> 2.86 V at GP27. Divider drains 286 uA"),
    ("C  PACK VOLTAGE SENSE  (20.06:1)",
     "C  PACK VOLTAGE SENSE  (21:1)"),
]


# ======================================================================================
#  Paren-balanced span finder that understands quoted strings
# ======================================================================================

def span_end(text, start):
    """Given index of an opening '(', return index just past its matching ')'."""
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
    raise ValueError("unbalanced parentheses")


def find_symbol_spans(text):
    """Yield (ref, start, end) for every placed symbol (one that has a lib_id)."""
    for m in re.finditer(r"\(symbol\b", text):
        s = m.start()
        try:
            e = span_end(text, s)
        except ValueError:
            continue
        body = text[s:e]
        if not re.search(r'\(lib_id\s+"', body):
            continue
        rm = re.search(r'\(property\s+"Reference"\s+"([^"]*)"', body)
        if rm:
            yield rm.group(1), s, e


def set_property(span_text, key, value):
    """Rewrite the quoted value of (property "key" "..."), return (text, changed)."""
    pat = re.compile(r'(\(property\s+"' + re.escape(key) + r'"\s+")([^"]*)(")')
    m = pat.search(span_text)
    if not m:
        return span_text, False
    if m.group(2) == value:
        return span_text, False
    return span_text[:m.start(2)] + value + span_text[m.end(2):], True


def add_property(span_text, key, value):
    """Insert a new property block, cloning the indentation of the Footprint block."""
    if re.search(r'\(property\s+"' + re.escape(key) + r'"', span_text):
        return set_property(span_text, key, value)
    m = re.search(r'([ \t]*)\(property\s+"Footprint"', span_text)
    if not m:
        return span_text, False
    indent = m.group(1)
    blk_start = m.start(0) + len(indent)
    blk_end = span_end(span_text, blk_start)
    at = re.search(r'\(at\s+([-\d.]+)\s+([-\d.]+)\s+(\d+)\)', span_text[blk_start:blk_end])
    pos = f"(at {at.group(1)} {at.group(2)} {at.group(3)})" if at else "(at 0 0 0)"
    new = (f'\n{indent}(property "{key}" "{value}"\n'
           f'{indent}\t{pos}\n'
           f'{indent}\t(effects\n'
           f'{indent}\t\t(font\n'
           f'{indent}\t\t\t(size 1.27 1.27)\n'
           f'{indent}\t\t)\n'
           f'{indent}\t\t(hide yes)\n'
           f'{indent}\t)\n'
           f'{indent})')
    return span_text[:blk_end] + new + span_text[blk_end:], True


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_sch"
    dry = "--dry-run" in sys.argv

    text = open(path, encoding="utf-8").read()
    original = text
    log = []

    # Rewrite symbol spans back-to-front so earlier offsets stay valid.
    spans = list(find_symbol_spans(text))
    for ref, s, e in sorted(spans, key=lambda t: -t[1]):
        span = text[s:e]
        if ref in VALUE_CHANGES:
            span, ch = set_property(span, "Value", VALUE_CHANGES[ref])
            if ch:
                log.append(f"{ref:5s} Value     -> {VALUE_CHANGES[ref]}")
        if ref in FOOTPRINTS:
            span, ch = set_property(span, "Footprint", FOOTPRINTS[ref])
            if ch:
                log.append(f"{ref:5s} Footprint -> {FOOTPRINTS[ref]}")
        if ref in LCSC:
            span, ch = add_property(span, "LCSC", LCSC[ref])
            if ch:
                log.append(f"{ref:5s} LCSC      -> {LCSC[ref]}")
        text = text[:s] + span + text[e:]

    for old, new in TEXT_CHANGES:
        if old in text:
            text = text.replace(old, new)
            log.append(f"note  text      -> {new[:52]}")

    print(f"{len(spans)} placed symbols found")
    print(f"{len(log)} edits")
    for line in sorted(log):
        print("   ", line)

    if dry:
        print("\n--dry-run: nothing written")
        return 0
    if text == original:
        print("\nno changes needed")
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = f"{path}.bak-{stamp}"
    shutil.copy2(path, backup)
    open(path, "w", encoding="utf-8").write(text)
    print(f"\nbackup written: {backup}")
    print(f"patched: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
