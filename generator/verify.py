#!/usr/bin/env python3
"""
Independent verifier for Logging_Current_Meter.kicad_sch.

This does NOT import the generator's in-memory state.  It re-reads both finished files
from disk and rebuilds the netlist geometrically, the way KiCad's netlister does: pin
connection points, wire endpoints and label anchors that share a coordinate are one
electrical node, and a label names that node.

That makes it a real check on the things most likely to be wrong:
  * the library -> sheet coordinate transform (a sign error would leave every stub
    floating beside its pin instead of on it),
  * stub endpoints not landing on label anchors,
  * pins colliding with unrelated nets,
  * pin-name -> net assignment typos,
  * the file not being parseable at all.

It then compares the recovered netlist against an independently written expectation.
"""

import sys
from collections import defaultdict

from sexp import loads, find, findall

TOL = 0.005


def key(x, y):
    return (round(x / TOL), round(y / TOL))


# ======================================================================================
#  Expected connectivity -- written from the design intent, not read from the generator.
# ======================================================================================
EXPECTED = {
    "PACK+":     {"J1.1", "J2.1", "U1.4"},
    "LOAD+":     {"J3.1", "J4.1", "U1.5", "R3.1"},
    "+5VS":      {"U1.1", "FB1.2", "C1.1", "C2.1", "R5.1", "#FLG1.1"},
    "I_RAW":     {"U1.3", "C3.1", "R1.1"},
    "I_SENSE":   {"R1.2", "R2.1", "C4.1", "U2.7"},
    "V5_SENSE":  {"R5.2", "R8.1", "C6.1", "U2.4"},
    "VPACK_MID": {"R3.2", "R4.1"},
    "VPACK_TAP": {"R4.2", "R6.1", "R7.1"},
    "V_PACK":    {"R7.2", "C5.1", "D1.3", "U2.6"},
    "T_POT_TOP": {"R10.2", "RV1.1"},
    "T_NODE":    {"RV1.2", "RV1.3", "J5.1", "R9.1"},
    "T_SENSE":   {"R9.2", "C7.1", "U2.5"},
    # C12/C13 removed -- the buttons are debounced in firmware, which is more flexible
    # and costs nothing, and SW1/SW2 are not timing-critical.
    "BTN1":      {"SW1.1", "R15.2", "U2.9"},
    "BTN2":      {"SW2.1", "R16.2", "U2.8"},
    "MCU_SCK":   {"U2.12", "R11.1"},
    "SPI_SCK":   {"R11.2", "J6.7", "J7.7"},
    "MCU_MOSI":  {"U2.13", "R12.1"},
    "SPI_MOSI":  {"R12.2", "J6.6", "J7.6"},
    "SPI_MISO":  {"R13.1", "J6.9", "J7.9"},
    "MCU_MISO":  {"R13.2", "U2.14"},
    "MCU_LED":   {"U2.18", "R14.1"},
    "DISP_LED":  {"R14.2", "J6.8", "J7.8"},
    "LCD_CS":    {"U2.15", "J6.3", "J7.3"},
    "LCD_RS":    {"U2.16", "J6.5", "J7.5"},
    "LCD_RST":   {"U2.17", "J6.4", "J7.4"},
    "SD_CS":     {"U2.19", "J6.14", "J7.14"},
    "CTP_SDA":   {"U2.20", "J6.12", "J7.12"},
    "CTP_SCL":   {"U2.21", "J6.10", "J7.10"},
    "CTP_RST":   {"U2.22", "J6.11", "J7.11"},
    "CTP_INT":   {"U2.23", "J6.13", "J7.13"},
    "DISP_VCC":  {"JP1.2", "J6.1", "J7.1"},
    # J8 is now a 3-pin AM32 servo header: signal, +5V (NOT connected), GND.  Telemetry
    # was dropped along with its series resistor R18 and clamp D2, which freed GP0 --
    # brought out to the J13 rail pad rather than left floating.
    "ESC_SIG":       {"J8.1", "R17.2"},
    "ESC_SIG_MCU":   {"U2.11", "R17.1"},
    "GP0":           {"U2.10", "J13.1"},
    "+5V":  {"U2.1", "FB1.1", "JP1.1", "C10.1", "C11.1", "#PWR3.1"},
    "+3V3": {"U2.3", "D1.2", "R10.1", "R15.1", "R16.1", "JP1.3",
             "C8.1", "C9.1", "#PWR2.1"},
    "GND": {"J1.2", "J2.2", "J3.2", "J4.2", "U1.2", "U2.2",
            "C1.2", "C2.2", "C3.2", "C4.2", "C5.2", "C6.2", "C7.2",
            "C8.2", "C9.2", "C10.2", "C11.2",
            "R2.2", "R6.2", "R8.2", "D1.1", "J5.2", "J8.3",
            "SW1.2", "SW2.2", "J6.2", "J7.2", "#PWR1.1", "#FLG2.1"},
}

# J6 (2.54 mm display header) was removed once the FPC connector J7 was committed to;
# it duplicated all 14 display signals and cost 35 mm of board width.
for _n in list(EXPECTED):
    EXPECTED[_n] = {t for t in EXPECTED[_n] if not t.startswith("J6.")}

# Test points and spare rail pads added by add_testpoints.py. Each joins an existing net
# via a label, so they must appear as extra pins on those nets and nowhere else.
# Reduced to the section 6 calibration set.  TP11-TP27 -- the 13 display-bus and 4
# user-IO probe points -- were removed; they were 8.5 % of the board, nearly double the
# area of every resistor and capacitor put together.  SPI_SCK/MOSI/MISO and DISP_LED are
# still probeable at R11-R14's pads and DISP_VCC at JP1, so the section 11 microSD MISO
# risk can still be diagnosed.
TP_ADDITIONS = {
    "PACK+": "TP1", "LOAD+": "TP2", "+5VS": "TP3", "I_RAW": "TP4", "I_SENSE": "TP5",
    "V5_SENSE": "TP6", "VPACK_TAP": "TP7", "V_PACK": "TP8", "T_NODE": "TP9",
    "T_SENSE": "TP10", "+3V3": "J9", "+5V": "J10",
}
# J13 is the GP0 expansion pad, already in EXPECTED above rather than added here.
for _net, _ref in TP_ADDITIONS.items():
    EXPECTED[_net] = EXPECTED[_net] | {_ref + ".1"}
EXPECTED["GND"] = EXPECTED["GND"] | {"J11.1", "J12.1"}


# Checked by GPIO NAME so a transposition inside the module symbol cannot hide behind a
# pin number that happens to match.
EXPECTED_MCU = {
    "5V": "+5V", "GND": "GND", "3V3": "+3V3",
    "GP29": "V5_SENSE", "GP28": "T_SENSE", "GP27": "V_PACK", "GP26": "I_SENSE",
    "GP15": "BTN2", "GP14": "BTN1",
    "GP0": "GP0", "GP1": "ESC_SIG_MCU",
    "GP2": "MCU_SCK", "GP3": "MCU_MOSI", "GP4": "MCU_MISO",
    "GP5": "LCD_CS", "GP6": "LCD_RS", "GP7": "LCD_RST", "GP8": "MCU_LED",
    "GP9": "SD_CS", "GP10": "CTP_SDA", "GP11": "CTP_SCL",
    "GP12": "CTP_RST", "GP13": "CTP_INT",
}

# From the Hosyond module silkscreen.
EXPECTED_DISPLAY = {
    1: "VCC", 2: "GND", 3: "LCD_CS", 4: "LCD_RST", 5: "LCD_RS", 6: "MOSI",
    7: "SCK", 8: "LED", 9: "MISO", 10: "CTP_SCL", 11: "CTP_RST", 12: "CTP_SDA",
    13: "CTP_INT", 14: "SD_CS",
}

# From the ACS770xCB datasheet terminal list.
EXPECTED_ACS = {"1": "VCC", "2": "GND", "3": "VIOUT", "4": "IP+", "5": "IP-"}


class DSU:
    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def collect_pins(sym_node):
    """Pins live in nested (symbol "NAME_1_1" ...) sub-units."""
    out = {}
    for pin in findall(sym_node, "pin"):
        etype = str(pin[1])
        pos = find(pin, "at")
        nm = find(pin, "name")
        num = find(pin, "number")
        if pos is None or num is None:
            continue
        out[str(num[1])] = (float(pos[1]), float(pos[2]),
                            int(pos[3]) if len(pos) > 3 else 0,
                            nm[1] if nm else "", etype)
    for sub in findall(sym_node, "symbol"):
        out.update(collect_pins(sub))
    return out


def main(schpath, libpath):
    problems, notes = [], []

    with open(libpath, encoding="utf-8") as f:
        libroot = loads(f.read())
    with open(schpath, encoding="utf-8") as f:
        schroot = loads(f.read())

    if str(libroot[0]) != "kicad_symbol_lib":
        problems.append("symbol library root token is not kicad_symbol_lib")
    if str(schroot[0]) != "kicad_sch":
        problems.append("schematic root token is not kicad_sch")

    schver = find(schroot, "version")
    libver = find(libroot, "version")
    print(f"schematic format version : {schver[1] if schver else '?'}")
    print(f"symbol lib format version: {libver[1] if libver else '?'}")

    geom = {}
    for s in findall(libroot, "symbol"):
        geom[str(s[1])] = collect_pins(s)

    # ---- pin coordinates on the sheet -------------------------------------------------
    # KiCad DefaultTransform (1,0,0,-1):  sheet = (sym.x + lib.x, sym.y - lib.y)
    pinpts = defaultdict(list)
    pin_meta = {}
    placed = []
    for ss in findall(schroot, "symbol"):
        libid = find(ss, "lib_id")
        if libid is None:
            continue                      # a lib_symbols entry, not a placement
        name = str(libid[1]).split(":", 1)[-1]
        pos = find(ss, "at")
        ref = None
        for p in findall(ss, "property"):
            if p[1] == "Reference":
                ref = p[2]
        if ref is None:
            problems.append(f"placed symbol {name} has no Reference property")
            continue
        placed.append(ref)
        if (int(pos[3]) if len(pos) > 3 else 0) != 0:
            problems.append(f"{ref}: rotated placement, verifier assumes 0 deg")
        if name not in geom:
            problems.append(f"{ref}: lib_id {name} not present in lib_symbols")
            continue
        for num, (lx, ly, ang, pname, etype) in geom[name].items():
            sx, sy = pos[1] + lx, pos[2] - ly
            tag = f"{ref}.{num}"
            pinpts[key(sx, sy)].append(tag)
            pin_meta[tag] = (name, pname, etype)

    # ---- wires -------------------------------------------------------------------------
    dsu = DSU()
    wires = findall(schroot, "wire")
    wire_ends = defaultdict(int)
    for w in wires:
        pts = [key(p[1], p[2]) for p in findall(find(w, "pts"), "xy")]
        for p in pts:
            wire_ends[p] += 1
        for a, bb in zip(pts, pts[1:]):
            dsu.union(a, bb)

    # ---- labels ------------------------------------------------------------------------
    label_pts = defaultdict(list)
    for lab in findall(schroot, "label"):
        pos = find(lab, "at")
        label_pts[key(pos[1], pos[2])].append(lab[1])

    byname = defaultdict(list)
    for k, texts in label_pts.items():
        for t in texts:
            byname[t].append(k)
    for t, nodes in byname.items():
        for a in nodes[1:]:
            dsu.union(nodes[0], a)

    root_names = defaultdict(set)
    for k, texts in label_pts.items():
        root_names[dsu.find(k)].update(texts)

    # Pins deliberately left unconnected.  Not a bare whitelist: the pin is only excused
    # if a `no_connect` marker is actually present at its coordinates, so deleting the
    # marker in KiCad brings the failure straight back.
    #
    # J8.2 is the ESC header's +5V pad.  It exists so an AM32 servo plug seats squarely
    # and is connected to nothing on purpose -- the RP2040-Zero ties VSYS to VBUS, so an
    # ESC BEC wired here would back-feed the USB host.
    NO_CONNECT_PINS = {"J8.2"}
    nc_pts = {key(nc[1], nc[2]) for nc in
              (find(n, "at") for n in findall(schroot, "no_connect")) if nc}
    excused = set()
    for k, tags in pinpts.items():
        for t in tags:
            if t in NO_CONNECT_PINS and k in nc_pts:
                excused.add(t)
    for t in NO_CONNECT_PINS - excused:
        problems.append(f"{t} is declared no-connect but has no no_connect marker")

    netlist = defaultdict(set)
    for k, tags in pinpts.items():
        names = root_names.get(dsu.find(k), set())
        if not names:
            if not (set(tags) - excused):
                continue
            problems.append(f"pin(s) with no net: {tags}")
            continue
        if len(names) > 1:
            problems.append(f"node carries conflicting labels {sorted(names)}: {tags}")
        netlist[sorted(names)[0]].update(tags)

    for k, tags in pinpts.items():
        if wire_ends.get(k, 0) == 0 and (set(tags) - excused):
            problems.append(f"pin(s) {tags} have no wire attached")
        if len(tags) > 1:
            problems.append(f"COINCIDENT PINS (implicit short): {tags}")
    for k, texts in label_pts.items():
        if wire_ends.get(k, 0) == 0:
            problems.append(f"label {texts} is not on a wire endpoint")

    got = {n: set(v) for n, v in netlist.items()}

    for n in sorted(set(EXPECTED) - set(got)):
        problems.append(f"net {n!r} expected but not found")
    for n in sorted(set(got) - set(EXPECTED)):
        problems.append(f"net {n!r} found but not expected")
    for n in sorted(set(EXPECTED) & set(got)):
        if EXPECTED[n] != got[n]:
            problems.append(f"net {n!r} mismatch\n"
                            f"        missing: {sorted(EXPECTED[n] - got[n])}\n"
                            f"        extra  : {sorted(got[n] - EXPECTED[n])}")

    # ---- MCU pin map, by GPIO name ------------------------------------------------------
    zero = geom.get("LCM:RP2040_ZERO") or geom.get("RP2040_ZERO", {})
    name_to_num = {v[3]: k for k, v in zero.items()}
    for gpio, expnet in EXPECTED_MCU.items():
        num = name_to_num.get(gpio)
        if num is None:
            problems.append(f"RP2040_ZERO has no pin named {gpio}")
            continue
        hit = [n for n, tags in got.items() if f"U2.{num}" in tags]
        if hit != [expnet]:
            problems.append(f"U2 {gpio} (pin {num}) on {hit}, expected [{expnet!r}]")

    # ---- fixed pin orders ---------------------------------------------------------------
    disp = geom.get("LCM:DISPLAY_14") or geom.get("DISPLAY_14", {})
    for num, nm in EXPECTED_DISPLAY.items():
        actual = disp.get(str(num), (None,) * 5)[3]
        if actual != nm:
            problems.append(f"DISPLAY_14 pin {num} is {actual!r}, expected {nm!r}")

    acs = geom.get("LCM:ACS770KCB-150U") or geom.get("ACS770KCB-150U", {})
    for num, nm in EXPECTED_ACS.items():
        actual = acs.get(num, (None,) * 5)[3]
        if actual != nm:
            problems.append(f"ACS770 pin {num} is {actual!r}, expected {nm!r} "
                            f"(datasheet terminal list)")

    # ---- ERC-lite -----------------------------------------------------------------------
    for n, tags in sorted(got.items()):
        if len(tags) < 2:
            problems.append(f"net {n!r} has only one pin: {sorted(tags)}")
        drivers = [t for t in tags if pin_meta.get(t, ("", "", ""))[2] == "power_out"]
        sinks = [t for t in tags if pin_meta.get(t, ("", "", ""))[2] == "power_in"]
        if sinks and not drivers:
            problems.append(f"net {n!r} has power_in pins {sorted(sinks)} but nothing "
                            f"driving it (needs a PWR_FLAG)")

    # ---- duplicate refs -----------------------------------------------------------------
    seen = set()
    for r in placed:
        if r in seen:
            problems.append(f"duplicate reference designator {r}")
        seen.add(r)

    # ---- body overlap (readability) -------------------------------------------------------
    boxes = []
    for ss in findall(schroot, "symbol"):
        libid = find(ss, "lib_id")
        if libid is None:
            continue
        name = str(libid[1]).split(":", 1)[-1]
        pos = find(ss, "at")
        ref = next((p[2] for p in findall(ss, "property") if p[1] == "Reference"), "?")
        g = geom.get(f"LCM:{name}") or geom.get(name, {})
        if not g:
            continue
        xs = [pos[1] + v[0] for v in g.values()]
        ys = [pos[2] - v[1] for v in g.values()]
        boxes.append((ref, min(xs) - 2, min(ys) - 2, max(xs) + 2, max(ys) + 2))
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            a, bb = boxes[i], boxes[j]
            if a[1] < bb[3] and bb[1] < a[3] and a[2] < bb[4] and bb[2] < a[4]:
                notes.append(f"symbol bodies overlap: {a[0]} and {bb[0]}")

    # =====================================================================================
    print(f"symbols   : {len(placed)}")
    print(f"wires     : {len(wires)}")
    print(f"labels    : {sum(len(v) for v in label_pts.values())}")
    print(f"nets      : {len(got)}")
    print(f"pin conns : {sum(len(v) for v in got.values())}")
    print()
    print("--- recovered netlist ---")
    for n in sorted(got, key=lambda s: (-len(got[s]), s)):
        tags = sorted(got[n], key=lambda t: (t.split('.')[0], int(t.split('.')[1])))
        print(f"  {n:16s} ({len(tags):2d})  {' '.join(tags)}")
    print()

    if notes:
        print("--- layout notes ---")
        for x in notes:
            print("  ", x)
        print()

    if problems:
        print(f"*** {len(problems)} PROBLEM(S) ***")
        for p in problems:
            print("  -", p)
        return 1
    print("PASS: file parses, and the recovered netlist matches the expectation exactly.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "Logging_Current_Meter.kicad_sch",
                  sys.argv[2] if len(sys.argv) > 2 else "LCM.kicad_sym"))
