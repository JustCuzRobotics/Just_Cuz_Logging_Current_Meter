#!/usr/bin/env python3
"""
Surgical builder for Logging_Current_Meter.kicad_pcb.

The board file is created by KiCad itself (Tools -> Update PCB from Schematic), because
the stock footprints live in the KiCad installation and cannot be synthesised here.  This
script then edits that file as TEXT, in named stages, so every uuid, every net binding and
KiCad's own formatting survive untouched.  Same philosophy as patch_sch.py.

Target format: KiCad 10 (version 20260206).

Stages, in order:
    sync      add footprints present in the schematic but missing from the board
    stackup   2 -> 4 copper layers, netclasses and DRC rules
    outline   Edge.Cuts board outline and mounting holes
    place     move every footprint to its designed position
    pour      bus zones, GND plane, via stitching, mask openings

Usage:
    python3 gen_pcb.py ..                 # run every stage
    python3 gen_pcb.py .. --stage sync    # run one stage
    python3 gen_pcb.py .. --dry-run
"""

import argparse
import datetime
import json
import math
import os
import re
import shutil
import sys
import uuid as _uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp

# ======================================================================================
#  Board geometry
# ======================================================================================
# The board's top-left corner in KiCad page coordinates.  Every dimension below is given
# in board-local millimetres (u across, v down) and converted through B() on the way out,
# so the whole layout can be shifted on the page by changing these two numbers only.
ORIGIN_X = 50.0
ORIGIN_Y = 50.0

BOARD_W = 95.0
# 62, not the 50 that DESIGN.md rev A decided.  Section 8's height budget costed the
# XT60 at "9.1 mm depth", which is its PAD extent; the F.Fab body outline is 18.2 mm
# (XT60PW-M) and 17.2 mm (XT60PW-F), confirmed against the vendor STEP models -- the
# connector is 15.5 x 18.2 mm of board area, 8 mm tall, mating horizontally.  Stacked
# with the ACS770's 19.5 mm courtyard the current block needs ~60 mm.  95 x 62 is still
# inside JLCPCB's <=100 x 100 mm bracket, so the change is free.
BOARD_H = 62.0
CORNER_R = 2.0

EDGE_WIDTH = 0.05


def B(u, v):
    """Board-local (u, v) -> KiCad page (x, y)."""
    return (round(ORIGIN_X + u, 4), round(ORIGIN_Y + v, 4))


def uid():
    return str(_uuid.uuid4())


def S(tag, *rest):
    """Build an s-expression node with a BARE tag.

    sexp._atom quotes any plain Python str, so ["uuid", ...] serialises as
    ("uuid" ...) -- a quoted string where KiCad expects a token.  KiCad rejects the
    whole file on it ("Expecting layer, hide, effects, locked, render_cache or tstamp.
    Got 'quoted string'"), and it is invisible in a diff unless you are looking for it.
    Every tag this module emits goes through here.
    """
    return [sexp.Sym(tag), *rest]


# ======================================================================================
#  Stage: sync -- footprints in the schematic but not yet on the board
# ======================================================================================
# RV1 and JP1 were added to the schematic after the last "Update PCB from Schematic",
# so the board carries 80 of the schematic's 82 footprints.  Rather than make the user
# round-trip through the GUI, both are built here from LCM.pretty with their nets, their
# schematic symbol uuid (the `path`, which is what keeps KiCad's forward annotation
# linked) and the same property block KiCad writes for every other footprint.
#
# Nets are taken from verify.py's EXPECTED table, which is written from the design intent
# rather than read back out of the generator.
MISSING = {
    "RV1": {
        "lib": "LCM:Potentiometer_Bourns_3362P_Vertical",
        "value": "100k",
        "path": "/f04b5aaf-1693-4408-8e5b-79aef167b8fa",
        "nets": {"1": "/T_POT_TOP", "2": "/T_NODE", "3": "/T_NODE"},
        "pinfunction": {"1": "1", "2": "2", "3": "3"},
        "pintype": {"1": "passive", "2": "passive", "3": "passive"},
        "attr": "through_hole",
    },
    "JP1": {
        "lib": "LCM:SolderJumper_3_P1.3mm_Open",
        "value": "5V / 3V3",
        "path": "/3ab7d48d-4376-4139-b1d3-5a0efbd52cb4",
        "nets": {"1": "+5V", "2": "/DISP_VCC", "3": "+3V3"},
        "pinfunction": {"1": "A", "2": "C", "3": "B"},
        "pintype": {"1": "passive", "2": "passive", "3": "passive"},
        "attr": "exclude_from_pos_files exclude_from_bom",
    },
    # 3-pin AM32 servo header.  Pad 2 carries NO NET on purpose: it exists so the plug
    # seats squarely, and connecting an ESC BEC to it would back-feed VBUS through the
    # RP2040-Zero.  The schematic has a matching no_connect marker, which verify.py
    # checks for.
    "J8": {
        "lib": "LCM:PinHeader_1x03_P2.54mm_Vertical",
        "value": "ESC 3-pin",
        "path": "/ea610439-85cc-42ad-bcd2-e3affb07ff53",
        "nets": {"1": "/ESC_SIG", "2": None, "3": "GND"},
        "pinfunction": {"1": "1", "2": "2", "3": "3"},
        "pintype": {"1": "passive", "2": "passive", "3": "passive"},
        "attr": "through_hole",
    },
    # GP0 expansion pad, freed by dropping ESC telemetry.
    "J13": {
        "lib": "LCM:RailPad_THT_D1.0mm",
        "value": "GP0",
        "path": "/057e6135-b724-44b2-8c79-3e746cbc806b",
        "nets": {"1": "/GP0"},
        "pinfunction": {"1": "1_1"},
        "pintype": {"1": "passive"},
        "attr": "exclude_from_pos_files exclude_from_bom",
    },
}


def _mod_body(modpath, ref, spec, at_xy, rot):
    """Convert a .kicad_mod (20231120 flavour) into a placed KiCad 10 footprint block.

    Done on the parsed s-expression tree rather than on text.  A line-oriented filter
    cannot see that `fp_text reference` spans two lines, and dropping only its first line
    leaves an orphan `(effects ...)` whose extra close-paren silently unbalances the
    whole board file.
    """
    root_node = sexp.loads(open(modpath).read())

    # Children that belong to a library file only, or that become properties on a
    # placed footprint rather than graphics.
    DROP = {"version", "generator", "layer", "attr", "descr", "tags"}

    graphics = []
    for child in root_node[2:]:
        if not isinstance(child, list):
            continue
        tag = str(child[0])
        if tag in DROP:
            continue
        if tag == "fp_text" and str(child[1]) in ("reference", "value"):
            continue
        if tag == "pad":
            name = str(child[1])
            if name not in spec["nets"]:
                raise SystemExit(f"{ref}: pad {name!r} has no net assignment")
            if spec["nets"][name] is None:
                # Deliberate no-connect; emitted with a pinfunction so verify_pcb.py
                # tells it apart from a mechanical pad and still reports it.
                graphics.append(list(child) + [
                    S("pinfunction", spec["pinfunction"][name]),
                    S("pintype", "no_connect"),
                    S("uuid", uid()),
                ])
                continue
            child = list(child) + [
                S("net", spec["nets"][name]),
                S("pinfunction", spec["pinfunction"][name]),
                # KiCad writes this one quoted -- (pintype "passive") -- unlike the
                # bare tokens elsewhere.  Match it rather than rely on the parser
                # accepting both.
                S("pintype", spec["pintype"][name]),
                S("uuid", uid()),
            ]
        elif tag in ("fp_line", "fp_rect", "fp_circle", "fp_poly", "fp_arc", "fp_text"):
            child = list(child) + [S("uuid", uid())]
        graphics.append(child)

    # KiCad 8 wrote (fill none) / (fill solid); 9 and 10 write (fill no) / (fill yes).
    # Both are still accepted on read, but keep the file internally consistent.
    def norm_fill(node):
        for i, c in enumerate(node):
            if isinstance(c, list):
                if str(c[0]) == "fill" and len(c) > 1 and str(c[1]) in ("none", "solid"):
                    node[i] = S("fill", sexp.Sym("no" if str(c[1]) == "none" else "yes"))
                else:
                    norm_fill(c)
    for g in graphics:
        norm_fill(g)

    body = "\n".join("\t\t" + l for g in graphics
                     for l in sexp.dumps(g, indent=0).splitlines())

    descr = sexp.val(root_node, "descr", default="")
    tags = sexp.val(root_node, "tags", default="")

    x, y = at_xy
    at = f"(at {x} {y})" if not rot else f"(at {x} {y} {rot})"

    head = f'''	(footprint "{spec["lib"]}"
		(layer "F.Cu")
		(uuid "{uid()}")
		{at}
		(descr "{descr}")
		(tags "{tags}")
		(property "Reference" "{ref}"
			(at 0 -5.6 0)
			(layer "F.SilkS")
			(uuid "{uid()}")
			(effects
				(font
					(size 0.8 0.8)
					(thickness 0.12)
				)
			)
		)
		(property "Value" "{spec["value"]}"
			(at 0 4.6 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uid()}")
			(effects
				(font
					(size 0.8 0.8)
					(thickness 0.12)
				)
			)
		)
		(property "Datasheet" ""
			(at 0 0 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uid()}")
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(property "Description" ""
			(at 0 0 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uid()}")
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(path "{spec["path"]}")
		(sheetname "/")
		(sheetfile "Logging_Current_Meter.kicad_sch")
		(attr {spec["attr"]})
		(duplicate_pad_numbers_are_jumpers no)
'''
    # Re-indent the library body to sit inside the placed footprint.
    body = "\n".join(("\t\t" + l.strip()) if l.strip() else "" for l in body.splitlines()
                     if l.strip())
    return head + body + '\n\t\t(embedded_fonts no)\n\t)\n'


def stage_sync(pcb, root):
    present = set(re.findall(r'\(property "Reference" "([^"]+)"', pcb))
    added = []
    for ref, spec in MISSING.items():
        if ref in present:
            print(f"  {ref}: already on the board, skipped")
            continue
        lib, name = spec["lib"].split(":", 1)
        modpath = os.path.join(root, f"{lib}.pretty", f"{name}.kicad_mod")
        if not os.path.exists(modpath):
            raise SystemExit(f"{ref}: footprint file not found: {modpath}")
        # Parked off-board for now; the place stage gives it its real position.
        block = _mod_body(modpath, ref, spec, (200.0, 60.0 + 12 * len(added)), 0)
        pcb = pcb[:pcb.rindex(")")] + block + ")\n"
        added.append(ref)
        print(f"  {ref}: added from {os.path.basename(modpath)}")
    return pcb


# ======================================================================================
#  Stage: prune -- footprints no longer in the schematic
# ======================================================================================
def stage_prune(pcb, root):
    """Delete board footprints whose reference is gone from the schematic.

    The counterpart to `sync`.  KiCad does this itself under "Update PCB from Schematic"
    with *delete extra footprints* ticked; doing it here keeps the board a pure function
    of the schematic without a round trip through the GUI.
    """
    sch = open(os.path.join(root, "Logging_Current_Meter.kicad_sch")).read()
    keep = schematic_footprint_refs(sch)
    print(f"  schematic has {len(keep)} footprint-bearing symbols")

    removed, restale = [], []
    while True:
        for m in re.finditer(r'\n\t\(footprint "([^"]+)"', pcb):
            s, e = _span(pcb, m.start() + 1)
            blk = pcb[s:e]
            ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
            if not ref:
                continue
            r = ref.group(1)
            if r not in keep:
                removed.append(r)
            elif m.group(1) != keep[r]:
                # The schematic reassigned this symbol to a different footprint.  The
                # board's copy is stale, so drop it and let `sync` rebuild it -- which
                # is what "Update PCB from Schematic" does.  J8 went 1x04 -> 1x03 this
                # way; leaving the old block would have kept a fourth pad carrying the
                # dead telemetry net.
                restale.append(f"{r} ({m.group(1)} -> {keep[r]})")
            else:
                continue
            pcb = pcb[:m.start()] + pcb[e:]
            break
        else:
            break

    if removed:
        print(f"  removed {len(removed)}: {', '.join(sorted(removed, key=_natural))}")
    if restale:
        print(f"  stale footprint, dropped for rebuild: {'; '.join(restale)}")
    if not removed and not restale:
        print("  nothing to remove")
    return pcb


def schematic_footprint_refs(sch):
    """{ref: footprint} for schematic symbols that carry a non-empty Footprint.

    Must be done per paren-balanced symbol block.  A regex that pairs "Reference" with
    the next "Footprint" silently runs past any symbol whose Footprint is empty -- every
    power flag on this sheet -- and pairs that reference with the FOLLOWING symbol's
    footprint.  The first version of this function did exactly that and deleted six live
    parts (C1, J11, R4, R10, R11, TP9) from the board.
    """
    refs = {}
    for m in re.finditer(r"\n\t\(symbol\n", sch):
        s, e = _span(sch, m.start() + 1)
        blk = sch[s:e]
        r = re.search(r'\(property\s*\n?\s*"Reference"\s*\n?\s*"([^"]*)"', blk)
        f = re.search(r'\(property\s*\n?\s*"Footprint"\s*\n?\s*"([^"]*)"', blk)
        if r and f and f.group(1) and not r.group(1).startswith("#"):
            refs[r.group(1)] = f.group(1)
    return refs


def _natural(s):
    m = re.match(r"([A-Za-z]+)(\d*)", s)
    return (m.group(1), int(m.group(2) or 0))


def _span(text, start):
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


# ======================================================================================
#  Stage: stackup -- 2 -> 4 copper layers, netclasses, DRC rules
# ======================================================================================
# KiCad 9 renumbered the layer enum: copper layers take the EVEN ordinals and everything
# else takes the odd ones.  F.Cu = 0 and B.Cu = 2 are already in the file KiCad wrote,
# and every even ordinal above 2 is unused, which is where the inner layers go.
LAYERS_4 = '''	(layers
		(0 "F.Cu" signal)
		(4 "In1.Cu" signal)
		(6 "In2.Cu" signal)
		(2 "B.Cu" signal)
'''

NETCLASSES = [
    # 0.5 mm, NOT the 2.0 mm rev A specified.  The ACS770's own two terminals are 1.00 mm
    # apart -- that is the moulded part, not something layout can change -- so a 2 mm
    # copper clearance is an unsatisfiable DRC error on every board.
    #
    # The 2 mm was never an electrical requirement anyway: DESIGN.md says "IPC-2221 only
    # demands ~0.4 mm at 60 V -- the 2 mm is for the assembly reality of flooding exposed
    # copper with solder".  That reasoning is right, but it applies to the SOLDERMASK
    # OPENINGS, which is where molten solder can actually flow, not to the copper.  So
    # copper gets 0.5 mm (comfortably above IPC) and the mask apertures are laid out
    # >= 2 mm apart in stage_pour.
    {"name": "HV_BUS", "clearance": 0.5, "track_width": 2.0,
     "via_diameter": 0.8, "via_drill": 0.4, "priority": 1},
    # GND takes the DEFAULT clearance, not 0.5 mm.
    #
    # KiCad resolves the clearance between two items as the LARGER of the constraints that
    # apply to either of them, so HV_BUS at 0.5 mm already guarantees 0.5 mm between
    # PACK+/LOAD+ and GND.  Putting 0.5 mm on GND as well bought nothing and applied it to
    # every GND pad on the board -- including J7 pin 2, on a 0.5 mm-pitch FPC connector
    # whose pads are 0.3 mm wide with 0.2 mm gaps by construction.  That made the display
    # connector unroutable: the rule demanded 0.5 mm where the part only has 0.2 mm.
    #
    # The class is kept for its track and via defaults, which are wanted on any hand-routed
    # GND, but its clearance goes back to Default.
    {"name": "GND_BUS", "clearance": 0.2, "track_width": 2.0,
     "via_diameter": 0.8, "via_drill": 0.4, "priority": 2},
    {"name": "PWR", "clearance": 0.2, "track_width": 0.5,
     "via_diameter": 0.6, "via_drill": 0.3, "priority": 3},
]

NETCLASS_PATTERNS = [
    ("HV_BUS", "/PACK+"),
    ("HV_BUS", "/LOAD+"),
    ("GND_BUS", "GND"),
    ("PWR", "+3V3"),
    ("PWR", "+5V"),
    ("PWR", "/+5VS"),
]


def stage_stackup(pcb, root, proj, dry_run=False):
    n = pcb.count('"In1.Cu"')
    if n:
        print("  board is already 4-layer, skipped")
    else:
        old = re.search(r'\t\(layers\n\t\t\(0 "F\.Cu" signal\)\n\t\t\(2 "B\.Cu" signal\)\n',
                        pcb)
        if not old:
            raise SystemExit("could not find the 2-layer (layers ...) block")
        pcb = pcb[:old.start()] + LAYERS_4 + pcb[old.end():]
        print("  copper layers: F.Cu / In1.Cu / In2.Cu / B.Cu")

    # ---- .kicad_pro: netclasses, patterns, rules ----
    with open(proj) as fh:
        pro = json.load(fh)

    classes = pro["net_settings"]["classes"]
    default = next(c for c in classes if c["name"] == "Default")
    keep = [c for c in classes if c["name"] == "Default"]
    for spec in NETCLASSES:
        c = dict(default)
        c.update(spec)
        c["pcb_color"] = "rgba(0, 0, 0, 0.000)"
        c["schematic_color"] = "rgba(0, 0, 0, 0.000)"
        keep.append(c)
    pro["net_settings"]["classes"] = keep
    pro["net_settings"]["netclass_patterns"] = [
        {"netclass": nc, "pattern": pat} for nc, pat in NETCLASS_PATTERNS
    ]
    print(f"  netclasses: {', '.join(s['name'] for s in NETCLASSES)}")

    rules = pro["board"]["design_settings"]["rules"]
    rules["min_copper_edge_clearance"] = 0.5
    rules["min_track_width"] = 0.2
    rules["min_via_diameter"] = 0.5
    rules["min_through_hole_diameter"] = 0.3

    if not dry_run:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        shutil.copy2(proj, f"{proj}.bak-{stamp}")
        with open(proj, "w") as fh:
            json.dump(pro, fh, indent=2)
            fh.write("\n")
        print(f"  wrote {os.path.basename(proj)}")
    return pcb


# ======================================================================================
#  Stage: outline -- Edge.Cuts, 95 x 50 mm, R2 corners
# ======================================================================================
def _gr_line(p0, p1):
    return f'''	(gr_line
		(start {p0[0]} {p0[1]})
		(end {p1[0]} {p1[1]})
		(stroke
			(width {EDGE_WIDTH})
			(type default)
		)
		(layer "Edge.Cuts")
		(uuid "{uid()}")
	)
'''


def _gr_arc(p0, pm, p1):
    return f'''	(gr_arc
		(start {p0[0]} {p0[1]})
		(mid {pm[0]} {pm[1]})
		(end {p1[0]} {p1[1]})
		(stroke
			(width {EDGE_WIDTH})
			(type default)
		)
		(layer "Edge.Cuts")
		(uuid "{uid()}")
	)
'''


def stage_outline(pcb, root):
    # Idempotent: drop any board-level outline this stage emitted before and redraw.
    # Only top-level (gr_line / gr_arc ...) are touched -- the fp_line Edge.Cuts inside
    # the mounting-hole footprints are part of those footprints and must survive.
    n_old = len(re.findall(r'\n\t\(gr_(?:line|arc)\n', pcb))
    if n_old:
        pcb = re.sub(r'\n\t\(gr_(?:line|arc)\n(?:.|\n)*?\n\t\)', '', pcb)
        print(f"  removed {n_old} existing outline segments")

    r = CORNER_R
    W, H = BOARD_W, BOARD_H
    k = r * (1 - math.sqrt(0.5))          # corner-arc midpoint inset

    seg = []
    # Straights, clockwise from the top-left corner.
    seg.append(_gr_line(B(r, 0), B(W - r, 0)))
    seg.append(_gr_line(B(W, r), B(W, H - r)))
    seg.append(_gr_line(B(W - r, H), B(r, H)))
    seg.append(_gr_line(B(0, H - r), B(0, r)))
    # Corner arcs.
    seg.append(_gr_arc(B(W - r, 0), B(W - k, k), B(W, r)))            # top-right
    seg.append(_gr_arc(B(W, H - r), B(W - k, H - k), B(W - r, H)))    # bottom-right
    seg.append(_gr_arc(B(r, H), B(k, H - k), B(0, H - r)))            # bottom-left
    seg.append(_gr_arc(B(0, r), B(k, k), B(r, 0)))                    # top-left

    pcb = pcb[:pcb.rindex(")")] + "".join(seg) + ")\n"
    print(f"  outline: {W} x {H} mm, R{r} corners, "
          f"origin ({ORIGIN_X}, {ORIGIN_Y})")
    return pcb


# ======================================================================================
#  Stage: place
# ======================================================================================
# (u, v, rotation) in board-local millimetres.  Grouped by function, with the reasoning
# for anything non-obvious written beside it.
#
# Frame: u runs left to right across the 95 mm, v runs top to bottom down the 62 mm.
# Current flows top -> bottom: J1/J2 in at the top, through U1, J3/J4 out at the bottom.

PLACEMENT = {}


def _put(**kw):
    PLACEMENT.update(kw)


# ---- current block, u in [0, 43] ----------------------------------------------------
# U1 rotated 90 deg: its two terminals (10 mm apart, natively side by side on the X
# axis) become vertically stacked, so the current direction is the board's short axis.
# The 24 mm body then projects RIGHT, putting VIOUT on pin 3 nearest the analog section.
#   at rot 90 the anchor-relative pads land at:
#     pad 4 IP+ (PACK+)  (-21.4, -5)      pad 5 IP- (LOAD+)  (-21.4, +5)
#     pad 1 VCC (0, +1.91)   pad 2 GND (0, 0)   pad 3 VIOUT (0, -1.91)
_put(U1=(41.5, 31.0, 90))

# J1/J3 carry the 150 A path.  Their pin 1 (PACK+ / LOAD+) lands at u = 17.6 and pin 2
# (GND) at u = 10.4, so the positive channel and the ground channel are each a straight
# vertical run.  M and F have mirrored pads, so J1 takes 180 deg and J3 takes 0 to put
# the same net on the same side, with both mating faces pointing off their board edge.
# u = 16.5 lands pin 1 at u = 20.10, which is exactly U1's terminal column, so the
# positive path is a straight vertical drop with no lateral jog at all.  It also clears
# the M3 courtyard at H1/H3, which u = 14.0 did not.
_put(J1=(16.5, 11.35, 180))     # XT60-M, input   crtyd u [8.5, 24.5]  v [0.75, 19.46]
_put(J3=(16.5, 45.65, 0))       # XT60-F, output  crtyd u [8.5, 24.5]  v [43.54, 61.25]

# The XT30 taps are a 30 A convenience, not part of the 150 A bus, so they do not have
# to sit inside the bus channels -- they reach GND through the In1 plane like any other
# component.  That frees them to sit clear of the XT60 bodies.
_put(J2=(33.0, 4.45, 0))        # XT30-M, input   crtyd u [26.1, 39.9]
_put(J4=(33.0, 51.6, 180))      # XT30-F, output  crtyd u [26.1, 39.9]

# ---- MCU ----------------------------------------------------------------------------
# rot -90 turns the module's USB-C end (local -Y) toward +X, so the socket reaches the
# right board edge.  Courtyard lands at X [69.5, 94.7], 0.3 mm inside the outline.
_put(U2=(81.5, 30.0, -90))

# ======================================================================================
#  Analog front end -- four horizontal lanes, not a column grid
# ======================================================================================
# The four ADC inputs are CONSECUTIVE pads on U2's top row, all at v = 22.38:
#
#     pad 7  I_SENSE   u = 76.42
#     pad 6  V_PACK    u = 78.96
#     pad 5  T_SENSE   u = 81.50
#     pad 4  V5_SENSE  u = 84.04
#
# Rev A filled a 3-column grid function-by-function, which read tidily but meant every
# one of those four nets had to leave a column vertically and then cross the whole board
# horizontally, through each other and through C10/RV1.  That is what made it hard to
# route.
#
# So the block is transposed: each channel gets a horizontal lane, and the lanes are
# ORDERED TOP TO BOTTOM TO MATCH U2's PAD ORDER LEFT TO RIGHT.  Every ADC net then runs
# right and slightly up to its own pad with no crossings, and no net has to pass through
# another channel's parts.
#
#   row 1  v = 20.5   ADC pin caps      C4   C5   C7   C6     <- closest free lane to U2
#   row 2  v = 24.5   last series part  R2   R7   R9   R8
#   row 3  v = 28.5   upstream parts    C3  R1  D1        R5
#   row 4  v = 32.5   sensor supply     FB1 C1  C2
#
# Column u for the four channels is 54.0 / 58.5 / 63.0 / 67.5, so each lane exits at the
# u closest to its own destination pad.
_ADC_U = (54.0, 58.5, 63.0, 67.5)      # I_SENSE, V_PACK, T_SENSE, V5_SENSE

# Row 1 -- the ADC pin caps.  These are the charge reservoirs that make the high-Z
# dividers work at all (sections 4.3, 4.4), so they take the lane nearest U2.
_put(C4=(_ADC_U[0], 20.5, 0), C5=(_ADC_U[1], 20.5, 0),
     C7=(_ADC_U[2], 20.5, 0), C6=(_ADC_U[3], 20.5, 0))

# Row 2 -- the last element in each chain before the pin.  R2 and R8 are divider lower
# legs to GND; R7 caps D1's clamp current at ~56 mA if 60 V ever reaches VPACK_TAP through
# a low impedance, and R9 is the thermistor series -- protection belongs at the MCU end,
# not at the source.  (An OPEN R6 is benign: ~280 uA through 200 k.  See DESIGN.md 4.3.)
_put(R2=(_ADC_U[0], 24.5, 0), R7=(_ADC_U[1], 24.5, 0),
     R9=(_ADC_U[2], 24.5, 0), R8=(_ADC_U[3], 24.5, 0))

# Row 3 -- upstream.  C3 is the 10 nF datasheet maximum sitting on VIOUT and is placed as
# close to U1 pin 3 (41.50, 29.09) as U1's courtyard allows -- 3.5 mm.
_put(C3=(45.0, 28.5, 0), R1=(49.5, 28.5, 0), R5=(67.5, 28.5, 0))
# D1 hand-placed on the board, kept.
_put(D1=(40.94, 20.0, 180))

# Row 4 -- the sensor's own supply.  +5VS is the ratiometric reference the whole current
# measurement depends on, so its filter sits by U1 pin 1 (41.50, 32.91), not by the MCU.
_put(FB1=(45.0, 32.5, 0), C1=(49.5, 32.5, 0), C2=(54.0, 32.5, 0))

# Pack-voltage divider: kept down on the LOAD+ pour it taps, so the only 60 V net in the
# electronics half is the one short hop from the pour into R3.  R3/R4/R6 sit ON the pour
# (u 17.6-33, v 31.3-61.5) below U1's courtyard; VPACK_TAP then runs up to R7/D1/C5 at
# the MCU end.  That node is 9.5 kOhm, so a longer run costs nothing, and it keeps the
# high voltage out of the signal lanes entirely.
# Just CLEAR of the LOAD+ mask opening, which ends at u = 32.0.  R3 at u = 30 put an
# 0805 inside the exposed strip that gets flooded with solder during bar assembly --
# verify_pcb.py check 7 now fails on that.
_put(R3=(35.0, 42.5, 0), R4=(39.5, 42.5, 0), R6=(44.0, 46.5, 0))

# Thermistor trim and its NTC connector, together so T_NODE stays short.
_put(RV1=(65.0, 41.0, 0), R10=(58.5, 44.0, 0), J5=(64.0, 47.0, 0))

# C10 is 100 uF of +5V bulk, and it feeds FB1 -> +5VS as well as the display backlight,
# so it sits beside the sensor-supply filter rather than out by the MCU where C11 already
# does the HF decoupling.
_put(C10=(48.0, 40.0, 0))

# ---- display bus (positions as hand-adjusted on the board) -------------------------
# R11-R14 are the series resistors on the shared SPI0 bus; R13 in particular is the
# 0 R placeholder in MISO that becomes a real resistor if the module's microSD does not
# tri-state cleanly (section 11, known risk).
_put(C8=(71.09, 42.0, 0), C9=(75.09, 42.0, 0), C11=(79.09, 42.0, 0))
_put(R11=(71.09, 47.0, 0), R12=(75.09, 47.0, 0), R13=(79.09, 47.0, 0),
     R14=(83.09, 42.0, 0))
_put(J7=(76.0, 58.0, 0), JP1=(69.0, 53.0, -90))

# ---- user IO -----------------------------------------------------------------------
# SW1 (view) and SW2 (zero/tare) are pressed by hand during calibration, so they go on
# the bottom edge rather than buried between the analog columns.
_put(SW1=(50.0, 57.0, 0), SW2=(60.0, 57.0, 0))
_put(R15=(47.0, 52.0, -90), R16=(57.0, 52.09, -90))
# J8 is now a 3-pin AM32 servo header (signal / +5V not-connected / GND).  R18 and D2
# went with the telemetry channel.
_put(J8=(89.0, 46.0, 0), R17=(89.0, 42.09, -90))

# ---- mounting ----------------------------------------------------------------------
# H1/H3 sit inside the GND channel, which is harmless -- a screw there is at the
# measurement reference.  H2/H4 are in the electronics area, well clear of the 60 V bus.
_put(H1=(4.0, 4.0, 0), H2=(91.0, 4.0, 0), H3=(4.0, 58.0, 0), H4=(91.0, 58.0, 0))

# ---- test points and rail pads: 5 columns x 3 rows ----------------------------------
# Down from 27 probe pads to 10.  The old block had a 501 mm2 bounding box -- 8.5 % of
# the board, nearly double the area of every resistor and capacitor on it put together.
# Kept as one block rather than distributed: calibration walks these nets in sequence
# with a DMM, so having them together is the point.
#
# Pitch is set by the SILKSCREEN, not the pads.  Each pad is labelled with its net name
# rather than its designator, because during calibration you are looking for I_SENSE,
# not TP5.  The longest, VPACK_TAP, is about 4.5 mm at 0.8 mm text, and the label sits
# 2.2 mm above the pad centre with the silk ring reaching 1.15 mm below it:
#   columns  9 mm  ->  4.5 mm clear between labels
#   rows     6 mm  ->  6 - 2.6 - 1.15 = 2.25 mm clear above each ring
TP_COLS = (45.0, 54.0, 63.0, 72.0, 81.0)
TP_ROWS = (4.0, 10.0, 16.0)

for _i in range(10):
    PLACEMENT[f"TP{_i + 1}"] = (TP_COLS[_i % 5], TP_ROWS[_i // 5], 0)
# Bottom row: the spare rails plus J13, the GP0 expansion pad freed by dropping ESC
# telemetry.  Plated 1.0 mm holes, so they take a probe ground spring or a soldered wire.
for _i, _ref in enumerate(["J9", "J10", "J11", "J12", "J13"]):
    PLACEMENT[_ref] = (TP_COLS[_i], TP_ROWS[2], 0)


def stage_place(pcb, root):
    seen, moved = set(), 0

    def repl(m):
        nonlocal moved
        block, ref = m.group(0), m.group(2)
        if ref not in PLACEMENT:
            return block
        seen.add(ref)
        u, v, rot = PLACEMENT[ref]
        x, y = B(u, v)
        at = f"(at {x} {y})" if not rot else f"(at {x} {y} {rot})"
        moved += 1
        return re.sub(r'\(at [-\d.]+ [-\d.]+(?: [-\d.]+)?\)', at, block, count=1)

    # Match from the footprint header through its Reference property -- that span holds
    # exactly one (at ...), the footprint's own position.
    pcb = re.sub(
        r'\(footprint "[^"]+"\n\t\t\(layer "[^"]+"\)\n\t\t\(uuid "[^"]+"\)\n'
        r'\t\t(\(at [^)]*\))(?:.|\n)*?\(property "Reference" "([^"]+)"',
        repl, pcb)

    missing = set(PLACEMENT) - seen
    if missing:
        raise SystemExit(f"placement table names footprints not on the board: "
                         f"{sorted(missing)}")

    present = set(re.findall(r'\(property "Reference" "([^"]+)"', pcb))
    unplaced = present - set(PLACEMENT)
    print(f"  placed {moved} footprints")
    if unplaced:
        print(f"  NOT in the placement table: {sorted(unplaced)}")
    return pcb


# ======================================================================================
#  Stage: renet -- net names the schematic has renamed
# ======================================================================================
# `prune` and `sync` reconcile which FOOTPRINTS are on the board.  Neither touches the
# pads of a footprint that survives, so renaming a net in the schematic leaves every
# existing pad still carrying the old name -- a dead net on a live pin, with nothing
# visibly wrong.  U2 pin 10 sat on /ESC_TELEM_MCU for exactly this reason after telemetry
# was dropped.  verify_pcb.py check 5 fails on any board net the schematic does not
# declare, so a rename that is not listed here cannot pass silently.
RENAMES = {
    "/ESC_TELEM_MCU": "/GP0",       # telemetry dropped; GP0 is now the free pin
}


def stage_renet(pcb, root):
    done = []
    for old, new in RENAMES.items():
        n = pcb.count(f'(net "{old}")')
        if n:
            pcb = pcb.replace(f'(net "{old}")', f'(net "{new}")')
            done.append(f"{old} -> {new} ({n} pad{'s' if n != 1 else ''})")
    print("  " + ("; ".join(done) if done else "nothing to rename"))
    return pcb


# ======================================================================================
#  Stage: silk -- label probe pads with their net, not their designator
# ======================================================================================
# During calibration (section 6) you are hunting for I_SENSE or T_NODE, not for "TP5".
# The net name is read off the footprint's own pad rather than from a table here, so it
# cannot drift out of step with the netlist.
SILK_LABEL = re.compile(r"^(TP\d+|J(?:9|1[0-3]))$")
SILK_OFFSET = -2.2          # mm above the pad centre, matching the footprint's own ref
SILK_SIZE = 0.8


def stage_silk(pcb, root):
    labelled = []

    def repl(m):
        blk = m.group(0)
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
        if not ref or not SILK_LABEL.match(ref.group(1)):
            return blk
        net = re.search(r'\(net "([^"]+)"\)', blk)
        if not net:
            return blk
        name = net.group(1).lstrip("/")

        if f'(fp_text user "{name}"' in blk:
            return blk

        # Hide the designator's silk text -- the net name takes its place, and two
        # strings stacked on one 1.5 mm pad is worse than none.
        blk = re.sub(
            r'(\(property "Reference" "[^"]+"\n\t\t\t\(at [^)]*\)\n\t\t\t'
            r'\(layer "F\.SilkS"\)\n)',
            r"\g<1>\t\t\t(hide yes)\n", blk, count=1)

        text = (f'\t\t(fp_text user "{name}"\n'
                f'\t\t\t(at 0 {SILK_OFFSET} 0)\n'
                f'\t\t\t(layer "F.SilkS")\n'
                f'\t\t\t(uuid "{uid()}")\n'
                f'\t\t\t(effects\n'
                f'\t\t\t\t(font\n'
                f'\t\t\t\t\t(size {SILK_SIZE} {SILK_SIZE})\n'
                f'\t\t\t\t\t(thickness 0.12)\n'
                f'\t\t\t\t)\n'
                f'\t\t\t)\n'
                f'\t\t)\n')
        blk = blk[:blk.rindex("\t\t(embedded_fonts")] + text \
            + blk[blk.rindex("\t\t(embedded_fonts"):]
        labelled.append((ref.group(1), name))
        return blk

    out, i = [], 0
    while True:
        m = re.search(r'\n\t\(footprint "', pcb[i:])
        if not m:
            out.append(pcb[i:])
            break
        s, e = _span(pcb, i + m.start() + 1)
        out.append(pcb[i:s])
        out.append(repl(re.match(r"(?s).*", pcb[s:e])))
        i = e
    pcb = "".join(out)

    print(f"  labelled {len(labelled)} pads with their net name")
    if labelled:
        print("   " + ", ".join(f"{r}={n}" for r, n in sorted(labelled, key=lambda t: _natural(t[0]))))
    return pcb


# ======================================================================================
#  Stage: refdes -- move passive designators off the silkscreen
# ======================================================================================
# 27 resistor and capacitor designators on a 95 x 62 board is more silkscreen than it is
# worth: they crowd the pads, they collide with each other at this density, and nobody
# reads "C9" off a finished board -- they read the schematic.  So they move to
# Dwgs.User (KiCad's "User.Drawings"), where they still print on documentation and stay
# visible while editing, but leave the physical silk clean.
#
# Dwgs.User rather than F.Fab, because these footprints already carry an
# `fp_text user "${REFERENCE}"` on F.Fab -- moving the property there would print the
# designator twice on the fab drawing.
#
# EXCEPTION: the 0 ohm placeholders keep their silkscreen.  R10 (thermistor upper leg,
# fit 91k for a stable trim) and R11-R14 (SPI series, fit 22R if the display bus rings,
# or a real resistor in MISO per the section 11 microSD risk) are the parts most likely
# to be changed by hand on an assembled board, and you cannot do that if you cannot find
# them.  Detected by Value == "0R", read off the board, not from a list here.
REFDES_MOVE = re.compile(r"^[RC]\d+$")
REFDES_KEEP_VALUE = "0R"
REFDES_LAYER = "Dwgs.User"


def stage_refdes(pcb, root):
    moved, kept = [], []
    out, i = [], 0
    while True:
        m = re.search(r'\n\t\(footprint "', pcb[i:])
        if not m:
            out.append(pcb[i:])
            break
        s, e = _span(pcb, i + m.start() + 1)
        blk = pcb[s:e]
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
        val = re.search(r'\(property "Value" "([^"]*)"', blk)
        if ref and REFDES_MOVE.match(ref.group(1)):
            if val and val.group(1).strip() == REFDES_KEEP_VALUE:
                kept.append(ref.group(1))
            else:
                new, n = re.subn(
                    r'(\(property "Reference" "[^"]+"\n\t\t\t\(at [^)]*\)\n\t\t\t'
                    r'\(layer ")F\.SilkS(")',
                    r"\g<1>" + REFDES_LAYER + r"\g<2>", blk, count=1)
                if n:
                    blk = new
                    moved.append(ref.group(1))
        out.append(pcb[i:s])
        out.append(blk)
        i = e
    pcb = "".join(out)

    print(f"  moved {len(moved)} designators to {REFDES_LAYER}")
    print(f"  kept on F.SilkS ({REFDES_KEEP_VALUE} placeholders): "
          f"{', '.join(sorted(kept, key=_natural))}")
    return pcb


# ======================================================================================
#  Stage: dnp -- keep hand-soldered parts out of the fab's machine outputs
# ======================================================================================
# Everything here is a through-hole part that a pick-and-place cannot fit, but all of them
# were landing in the position file JLCPCB places from, and in the BOM it sources from.
# Caught by looking at the uploaded gerbers, not by any check in this repo.
#
#   U2   Waveshare RP2040-Zero module -- not a line item any fab can buy
#   J1-J4  XT60 / XT30 power connectors      J5, J8  2.54 mm headers
#   RV1  3362P trimpot
#
# `dnp` tells the fab not to fit it, `exclude_from_pos_files` keeps it out of the CPL, and
# `exclude_from_bom` keeps it out of the sourcing list.  BOM.md still lists all of them
# under "hand-soldered" -- that document is for YOUR ordering, and gen_bom.py builds it
# from its own HAND table rather than from these flags, so the two do not fight.
DNP_PARTS = {"J1", "J2", "J3", "J4", "J5", "J8", "RV1", "U2"}
DNP_FLAGS = ["exclude_from_pos_files", "exclude_from_bom", "dnp"]


def stage_dnp(pcb, root):
    done, already = [], []
    out, i = [], 0
    while True:
        m = re.search(r'\n\t\(footprint "', pcb[i:])
        if not m:
            out.append(pcb[i:])
            break
        s, e = _span(pcb, i + m.start() + 1)
        blk = pcb[s:e]
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
        if ref and ref.group(1) in DNP_PARTS:
            am = re.search(r"\n\t\t\(attr ([^)]*)\)", blk)
            if am:
                have = am.group(1).split()
                add = [f for f in DNP_FLAGS if f not in have]
                if add:
                    blk = (blk[:am.start()]
                           + f"\n\t\t(attr {' '.join(have + add)})"
                           + blk[am.end():])
                    done.append(ref.group(1))
                else:
                    already.append(ref.group(1))
        out.append(pcb[i:s])
        out.append(blk)
        i = e
    pcb = "".join(out)

    if done:
        print(f"  flagged {len(done)}: {', '.join(sorted(done, key=_natural))}")
    if already:
        print(f"  already flagged: {', '.join(sorted(already, key=_natural))}")
    missing = DNP_PARTS - set(done) - set(already)
    if missing:
        raise SystemExit(f"not found on the board: {sorted(missing)}")
    return pcb


# ======================================================================================
#  Stage: pour -- bus zones, GND plane, mask openings, via stitching
# ======================================================================================
# Geometry, in board-local mm.  The two vertical channels are set by the connector pads:
# J1/J3 put PACK+/LOAD+ at u = 20.10 and GND at u = 12.90, and U1's terminal groups
# occupy u [17.85, 22.35].
#
#   GND channel      u [0.5, 15.5]      full height
#   positive channel u [17.6, 33.0]     split by U1's terminals into PACK+ and LOAD+
#
# The 2.1 mm between the two channel outlines is the assembly separation; copper
# clearance itself is the 0.5 mm netclass value (see NETCLASSES).
GND_CH = (0.5, 0.5, 15.5, 61.5)

# U1's terminals sit at v [21.50, 30.50] (PACK+) and v [31.50, 40.50] (LOAD+), 1.00 mm
# apart.  The two pours stop 0.2 mm clear of each terminal group, giving a 0.6 mm gap --
# tight, but it is the part's own geometry and 0.5 mm clearance permits it.
PACKP_CH = (17.6, 0.5, 33.0, 30.70)
LOADP_CH = (17.6, 31.30, 33.0, 61.5)

# Soldermask openings -- where a bar or a solder flood can actually be applied.  Kept
# >= 2 mm apart and clear of every pad.  The positive openings are short because this
# layout put J1 only ~8 mm from U1's terminals; see the revised copper budget in
# DESIGN.md section 8.
# Each opening spans its segment's whole current path, pad edge to pad edge -- a bar can
# only carry current where it is bonded, so an opening shorter than the run leaves the
# uncovered part at full PCB resistance.  GND is the long one (24 mm from J1.2 to J3.2)
# and is where a bar actually earns its keep; the positive segments are only 1.8 mm and
# 3.3 mm of pour, because this layout put J1/J3 almost against U1's terminals.
MASK_OPENINGS = [
    ("GND",    (2.0, 19.0, 14.0, 44.0)),
    ("/PACK+", (17.8, 16.0, 32.0, 21.5)),
    ("/LOAD+", (17.8, 40.5, 32.0, 45.0)),
]

STITCH_PITCH = 2.5
STITCH_CLEAR = 1.6          # centre-to-pad-edge keepout for a 0.8 mm via
VIA_SIZE, VIA_DRILL = 0.8, 0.4

ZONE_LAYERS = {
    "GND":    ["F.Cu", "B.Cu"],
    "/PACK+": ["F.Cu", "In2.Cu", "B.Cu"],
    "/LOAD+": ["F.Cu", "In2.Cu", "B.Cu"],
}


def _zone(net, layers, rect, priority=1, name=""):
    u0, v0, u1, v1 = rect
    pts = [B(u0, v0), B(u1, v0), B(u1, v1), B(u0, v1)]
    lay = " ".join(f'"{l}"' for l in layers)
    xy = "\n".join(f"\t\t\t\t(xy {x} {y})" for x, y in pts)
    return f'''	(zone
		(net "{net}")
		(layers {lay})
		(uuid "{uid()}")
		(name "{name}")
		(hatch edge 0.5)
		(priority {priority})
		(connect_pads
			(clearance 0.3)
		)
		(min_thickness 0.3)
		(filled_areas_thickness no)
		(fill yes
			(thermal_gap 0.3)
			(thermal_bridge_width 0.8)
			(island_removal_mode 1)
			(island_area_min 5)
		)
		(polygon
			(pts
{xy}
			)
		)
	)
'''


def _mask(rect, layer):
    u0, v0, u1, v1 = rect
    pts = [B(u0, v0), B(u1, v0), B(u1, v1), B(u0, v1)]
    xy = "\n".join(f"\t\t\t\t(xy {x} {y})" for x, y in pts)
    return f'''	(gr_poly
		(pts
{xy}
		)
		(stroke
			(width 0)
			(type solid)
		)
		(fill yes)
		(layer "{layer}")
		(uuid "{uid()}")
	)
'''


def _via(x, y, net):
    return f'''	(via
		(at {x} {y})
		(size {VIA_SIZE})
		(drill {VIA_DRILL})
		(layers "F.Cu" "B.Cu")
		(net "{net}")
		(uuid "{uid()}")
	)
'''


def stage_pour(pcb, root):
    # Idempotent: drop anything this stage emitted before.
    n = len(re.findall(r"\n\t\((?:zone|via)\n", pcb))
    if n:
        pcb = re.sub(r"\n\t\((?:zone|via)\n(?:.|\n)*?\n\t\)", "", pcb)
        pcb = re.sub(r"\n\t\(gr_poly\n(?:.|\n)*?\n\t\)", "", pcb)
        print(f"  removed {n} existing zones/vias")

    # ---- every pad on the board, so stitching can avoid them ----
    obstacles = []
    for m in re.finditer(r'\n\t\(footprint "', pcb):
        s, e = _span(pcb, m.start() + 1)
        blk = sexp.loads(pcb[s:e])
        at = sexp.find(blk, "at")
        fx, fy = at[1] - ORIGIN_X, at[2] - ORIGIN_Y
        fr = at[3] if len(at) > 3 else 0
        for pd in sexp.findall(blk, "pad"):
            pat, psz = sexp.find(pd, "at"), sexp.find(pd, "size")
            prot = pat[3] if len(pat) > 3 else 0
            a = math.radians(fr)
            dx = pat[1] * math.cos(a) + pat[2] * math.sin(a)
            dy = -pat[1] * math.sin(a) + pat[2] * math.cos(a)
            w, h = psz[1], psz[2]
            if abs(((fr + prot) % 180) - 90) < 45:
                w, h = h, w
            obstacles.append((sexp.val(pd, "net"),
                              fx + dx - w / 2, fy + dy - h / 2,
                              fx + dx + w / 2, fy + dy + h / 2))

    def blocked(u, v, net):
        for onet, x0, y0, x1, y1 in obstacles:
            if onet == net:
                continue          # same net: a via on our own pad is harmless, skip it
            if (x0 - STITCH_CLEAR <= u <= x1 + STITCH_CLEAR
                    and y0 - STITCH_CLEAR <= v <= y1 + STITCH_CLEAR):
                return True
        return False

    def on_own_pad(u, v, net):
        for onet, x0, y0, x1, y1 in obstacles:
            if onet == net and x0 <= u <= x1 and y0 <= v <= y1:
                return True
        return False

    items = []

    # ---- In1.Cu: full-board GND plane, the return and the reference for every signal --
    items.append(_zone("GND", ["In1.Cu"], (0.8, 0.8, BOARD_W - 0.8, BOARD_H - 0.8),
                       priority=0, name="GND plane In1"))

    # ---- the three bus channels, on outer layers ----
    for net, rect, nm in (("GND", GND_CH, "GND bus"),
                          ("/PACK+", PACKP_CH, "PACK+ bus"),
                          ("/LOAD+", LOADP_CH, "LOAD+ bus")):
        items.append(_zone(net, ZONE_LAYERS[net], rect, priority=1, name=nm))

    # ---- soldermask openings, front and back ----
    for net, rect in MASK_OPENINGS:
        for layer in ("F.Mask", "B.Mask"):
            items.append(_mask(rect, layer))

    # ---- via stitching ----
    counts = {}
    for net, rect in (("GND", GND_CH), ("/PACK+", PACKP_CH), ("/LOAD+", LOADP_CH)):
        u0, v0, u1, v1 = rect
        n_net = 0
        u = u0 + STITCH_PITCH
        while u < u1 - STITCH_PITCH / 2:
            v = v0 + STITCH_PITCH
            while v < v1 - STITCH_PITCH / 2:
                if not blocked(u, v, net) and not on_own_pad(u, v, net):
                    x, y = B(u, v)
                    items.append(_via(x, y, net))
                    n_net += 1
                v += STITCH_PITCH
            u += STITCH_PITCH
        counts[net] = n_net

    pcb = pcb[:pcb.rindex(")")] + "".join(items) + ")\n"
    print(f"  4 zones (In1 GND plane + 3 bus channels), "
          f"{len(MASK_OPENINGS) * 2} mask openings")
    print(f"  stitching vias: " + ", ".join(f"{k}={v}" for k, v in counts.items()))
    return pcb


# ======================================================================================
#  Driver
# ======================================================================================
# Order matters: `prune` runs first so that a footprint the schematic has reassigned is
# dropped before `sync` rebuilds it. Reversed, sync would skip the still-present stale
# copy and prune would then delete it with nothing left behind.
STAGES = {
    "prune": stage_prune,
    "sync": stage_sync,
    "stackup": stage_stackup,
    "renet": stage_renet,
    "outline": stage_outline,
    "place": stage_place,
    "silk": stage_silk,
    "refdes": stage_refdes,
    "dnp": stage_dnp,
    "pour": stage_pour,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", help="project directory")
    ap.add_argument("--stage", action="append", choices=list(STAGES) + ["all"],
                    default=None)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="run place/pour even though the board has routed tracks")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    pcbpath = os.path.join(root, "Logging_Current_Meter.kicad_pcb")
    propath = os.path.join(root, "Logging_Current_Meter.kicad_pro")
    pcb = open(pcbpath).read()
    before = pcb

    stages = args.stage or ["all"]
    if "all" in stages:
        stages = list(STAGES)

    # ---- refuse to clobber hand routing -------------------------------------------------
    # `place` moves footprints out from under any track already attached to them, and
    # `pour` deletes and rebuilds every zone and via.  Once there is copper on the board
    # those two stages destroy work that is not reproducible from this file.
    DESTRUCTIVE = {"place", "pour"}
    n_tracks = len(re.findall(r"\n\t\(segment\n", pcb))
    if n_tracks and DESTRUCTIVE & set(stages) and not args.force:
        raise SystemExit(
            f"REFUSING TO RUN: the board has {n_tracks} routed tracks, and "
            f"{sorted(DESTRUCTIVE & set(stages))} would destroy them.\n"
            f"  - to run the harmless stages only:  --stage "
            f"{' --stage '.join(s for s in stages if s not in DESTRUCTIVE)}\n"
            f"  - to override deliberately:         --force\n"
            f"Before ever forcing it, re-sync the PLACEMENT table from the board, or the "
            f"run will also undo every hand rotation and reposition.")

    for name in stages:
        print(f"[{name}]")
        fn = STAGES[name]
        if name == "stackup":
            pcb = fn(pcb, root, propath, args.dry_run)
        else:
            pcb = fn(pcb, root)

    if args.dry_run:
        print("\n-- dry run, nothing written --")
        return

    if pcb != before:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        shutil.copy2(pcbpath, f"{pcbpath}.bak-{stamp}")
        with open(pcbpath, "w") as fh:
            fh.write(pcb)
        print(f"\nwrote {pcbpath}")
    else:
        print("\nno change to the board file")


if __name__ == "__main__":
    main()
