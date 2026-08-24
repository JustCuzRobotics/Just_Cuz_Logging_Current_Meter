#!/usr/bin/env python3
"""
Independent checker for Logging_Current_Meter.kicad_pcb.

Re-reads the finished board off disk and re-derives every position from it, so it can
disagree with the generator.  Sibling to verify.py, which does the same job for the
schematic netlist.

Checks:
  1. every schematic footprint is on the board, exactly once
  2. every pad carries a net, and the set of nets matches the schematic
  3. every pad and courtyard is inside the outline, with edge clearance
  4. no two courtyards overlap
  5. the exposed bus channels keep their separation

Usage:  python3 verify_pcb.py ../Logging_Current_Meter.kicad_pcb ../Logging_Current_Meter.kicad_sch
"""

import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp

EDGE_CLEARANCE = 0.5          # mm, copper to board edge
COURTYARD_TOL = 0.001         # mm, ignore rounding-level touching

# KiCad invents a placeholder net for every pad it knows is deliberately unconnected,
# named like `unconnected-(J8-Pad2)`.  The schematic never declares those, so they are not
# stale names -- they are KiCad's own bookkeeping.
AUTO_NET = re.compile(r"^unconnected-\(")

# Courtyards that are allowed to overlap, with the reason.  Nothing goes in here without
# a physical justification -- an entry is a decision, not a way to silence the check.
ALLOWED_OVERLAP = {
    # Test points are bare 1.5 mm pads with a courtyard barely larger than the pad; the
    # 4 mm grid is deliberate and they never carry a body.
}


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


def rot(x, y, deg):
    a = math.radians(deg)
    return (x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a))


def fp_ref(fp):
    return next((p[2] for p in sexp.findall(fp, "property")
                 if p[1] == "Reference"), "?")


def shape_points(g, fx, fy, fr):
    """Corner points of a footprint graphic, in board coordinates."""
    tag = str(g[0])
    pts = []
    if tag in ("fp_line", "fp_rect"):
        s, e = sexp.find(g, "start"), sexp.find(g, "end")
        if tag == "fp_rect":
            local = [(s[1], s[2]), (e[1], s[2]), (e[1], e[2]), (s[1], e[2])]
        else:
            local = [(s[1], s[2]), (e[1], e[2])]
        pts = local
    elif tag == "fp_circle":
        c, e = sexp.find(g, "center"), sexp.find(g, "end")
        r = math.dist((c[1], c[2]), (e[1], e[2]))
        pts = [(c[1] - r, c[2] - r), (c[1] + r, c[2] - r),
               (c[1] + r, c[2] + r), (c[1] - r, c[2] + r)]
    elif tag == "fp_poly":
        p = sexp.find(g, "pts")
        pts = [(xy[1], xy[2]) for xy in sexp.findall(p, "xy")] if p else []
    elif tag == "fp_arc":
        pts = [(sexp.find(g, t)[1], sexp.find(g, t)[2])
               for t in ("start", "mid", "end") if sexp.find(g, t)]
    return [(fx + dx, fy + dy) for dx, dy in (rot(x, y, fr) for x, y in pts)]


def courtyard_box(fp, fallback=True):
    """Courtyard bounding box, or the pad bounding box if the footprint has no courtyard.

    Several imported footprints (SW_TS-1187A, the FPC connector) carry no F.CrtYd at all.
    Returning None for those would silently exempt real, physical parts from the overlap
    check -- which is exactly the class of mistake this file exists to catch -- so they
    fall back to their pad extent instead.
    """
    at = sexp.find(fp, "at")
    fx, fy = at[1], at[2]
    fr = at[3] if len(at) > 3 else 0
    pts = []
    for g in fp[1:]:
        if isinstance(g, list) and str(g[0]).startswith("fp_") \
                and str(sexp.val(g, "layer")) in ("F.CrtYd", "B.CrtYd"):
            pts += shape_points(g, fx, fy, fr)
    if pts:
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        return (min(xs), min(ys), max(xs), max(ys))
    if not fallback:
        return None
    pb = pad_boxes(fp)
    if not pb:
        return None
    return (min(p[3] for p in pb), min(p[4] for p in pb),
            max(p[5] for p in pb), max(p[6] for p in pb))


def pad_boxes(fp):
    at = sexp.find(fp, "at")
    fx, fy = at[1], at[2]
    fr = at[3] if len(at) > 3 else 0
    out = []
    for pd in sexp.findall(fp, "pad"):
        pat, psz = sexp.find(pd, "at"), sexp.find(pd, "size")
        # A pad's third `at` value, when present, is its ABSOLUTE rotation -- KiCad has
        # already folded the footprint's rotation into it.  When absent, the pad simply
        # inherits the footprint's rotation.  Adding the two (which this did) double-counts
        # and silently un-rotates every pad on a rotated footprint: R1/R15/R17/JP1 all came
        # out with their width and height swapped back, which fed wrong numbers to the
        # clearance, courtyard, mask-opening and via checks.
        prot = pat[3] if len(pat) > 3 else fr
        dx, dy = rot(pat[1], pat[2], fr)
        w, h = psz[1], psz[2]
        if abs((prot % 180) - 90) < 45:
            w, h = h, w
        cx, cy = fx + dx, fy + dy
        out.append((str(pd[1]), sexp.val(pd, "net"), str(pd[2]),
                    cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2))
    return out


def overlap(a, b):
    ox = min(a[2], b[2]) - max(a[0], b[0])
    oy = min(a[3], b[3]) - max(a[1], b[1])
    return (ox, oy) if ox > COURTYARD_TOL and oy > COURTYARD_TOL else None


def main(pcbpath, schpath, dump=False):
    raw = open(pcbpath).read()
    root = sexp.loads(raw)
    fps = sexp.findall(root, "footprint")
    fails, warns = [], []

    # ---- 0. syntax lint ----
    # Every s-expression tag must be a BARE token.  A quoted one -- ("uuid" "...") --
    # parses fine here but KiCad refuses to open the whole file, and the difference is
    # one character wide in a 300 kB diff.  Cheapest possible check, so it runs first.
    bad = [(raw[:m.start()].count("\n") + 1, m.group(1))
           for m in re.finditer(r'\(\s*"([^"]+)"', raw)]
    if bad:
        for line, tag in bad[:8]:
            fails.append(f'quoted tag ("{tag}" ...) at line {line} -- must be bare')
        if len(bad) > 8:
            fails.append(f"... and {len(bad) - 8} more quoted tags")
    print(f"[0] syntax: {len(bad)} quoted tags")
    if bad and not dump:
        print()
        for f in fails:
            print(f"  FAIL  {f}")
        print(f"\n{len(fails)} problem(s)")
        return 1

    if dump:
        ox = min(sexp.find(g, "start")[1] for g in sexp.findall(root, "gr_line")
                 if sexp.val(g, "layer") == "Edge.Cuts")
        oy = min(sexp.find(g, "start")[2] for g in sexp.findall(root, "gr_line")
                 if sexp.val(g, "layer") == "Edge.Cuts")
        rows = []
        for fp in fps:
            cb = courtyard_box(fp)
            at = sexp.find(fp, "at")
            r = at[3] if len(at) > 3 else 0
            rows.append((fp_ref(fp), at[1] - ox, at[2] - oy, r, cb))
        for ref, u, v, r, cb in sorted(rows, key=lambda t: t[0]):
            if cb:
                print(f"{ref:6s} at ({u:6.2f},{v:6.2f}) rot {r:3}  "
                      f"crtyd u[{cb[0]-ox:6.2f},{cb[2]-ox:6.2f}] "
                      f"v[{cb[1]-oy:6.2f},{cb[3]-oy:6.2f}]  "
                      f"{cb[2]-cb[0]:5.2f} x {cb[3]-cb[1]:5.2f}")
            else:
                print(f"{ref:6s} at ({u:6.2f},{v:6.2f}) rot {r:3}  no courtyard")
        return 0

    # ---- 1. footprint inventory vs the schematic ----
    sch = open(schpath).read()
    sch_refs = set()
    for m in re.finditer(r'\(property\s*\n?\s*"Reference"\s*\n?\s*"([^"]+)"', sch):
        r = m.group(1)
        if not r.startswith("#"):
            sch_refs.add(r)
    # Only symbols that were given a footprint end up on the board.  This must be read
    # per paren-balanced symbol block: a regex pairing "Reference" with the next
    # "Footprint" runs straight past every symbol whose footprint is empty (the power
    # flags) and pairs its reference with the following symbol's footprint.
    sch_fp = set()
    for m in re.finditer(r"\n\t\(symbol\n", sch):
        s, e = _span(sch, m.start() + 1)
        blk = sch[s:e]
        r = re.search(r'\(property\s*\n?\s*"Reference"\s*\n?\s*"([^"]*)"', blk)
        f = re.search(r'\(property\s*\n?\s*"Footprint"\s*\n?\s*"([^"]*)"', blk)
        if r and f and f.group(1) and not r.group(1).startswith("#"):
            sch_fp.add(r.group(1))

    board_refs = [fp_ref(f) for f in fps]
    dupes = {r for r in board_refs if board_refs.count(r) > 1}
    if dupes:
        fails.append(f"duplicate references on the board: {sorted(dupes)}")
    missing = sch_fp - set(board_refs)
    extra = set(board_refs) - sch_fp
    if missing:
        fails.append(f"in the schematic but not on the board: {sorted(missing)}")
    if extra:
        fails.append(f"on the board but not in the schematic: {sorted(extra)}")
    print(f"[1] footprints: {len(fps)} on the board, {len(sch_fp)} in the schematic")

    # ---- 2. nets ----
    # A pad with no net but also no pinfunction is a mechanical feature -- a connector
    # shroud leg, an FPC hold-down tab -- and correctly carries no net.  A pad marked
    # (pintype "no_connect") is deliberately unconnected.  Anything else with a
    # pinfunction and no net is a real, silent open circuit.
    board_nets, netless, mech, nc = set(), [], [], []
    for fp in fps:
        ref = fp_ref(fp)
        for pd in sexp.findall(fp, "pad"):
            if str(pd[2]) == "np_thru_hole":
                continue
            n = sexp.val(pd, "net")
            tag = f"{ref}.{pd[1]}"
            # Once KiCad has read the schematic's no_connect marker it writes the pintype
            # as "passive+no_connect" and gives the pad an auto-generated placeholder net
            # named unconnected-(REF-PadN).  Both are KiCad doing the right thing, so the
            # pad is a no-connect whether or not it still looks netless.
            is_nc = "no_connect" in str(sexp.val(pd, "pintype") or "")
            if n is not None and AUTO_NET.match(n):
                n = None
            if n is None:
                if is_nc:
                    nc.append(tag)
                elif sexp.val(pd, "pinfunction") is None:
                    mech.append(tag)
                else:
                    netless.append(tag)
            else:
                board_nets.add(n)
    if netless:
        fails.append(f"electrical pads with no net ({len(netless)}): {sorted(netless)}")
    print(f"[2] nets: {len(board_nets)} distinct; {len(mech)} mechanical pads and "
          f"{len(nc)} no-connect pads {sorted(nc)} with no net (expected)")

    # ---- 5. every board net must be one the schematic actually declares ----
    # Renaming a net in the schematic does not touch pads on footprints that survive the
    # sync, so a pad can sit on a net name nothing declares any more -- a dead net on a
    # live pin.  U2 pin 10 did exactly that after ESC telemetry was dropped.
    declared = {m.group(1) for m in re.finditer(r'\(label "([^"]+)"', sch)}
    for m in re.finditer(r"\n\t\(symbol\n", sch):
        s, e = _span(sch, m.start() + 1)
        blk = sch[s:e]
        if re.search(r'\(lib_id "LCM:(GND|PWR_FLAG)"\)', blk) or "(power)" in blk:
            v = re.search(r'\(property\s*\n?\s*"Value"\s*\n?\s*"([^"]*)"', blk)
            if v:
                declared.add(v.group(1))
    declared |= {"GND", "+3V3", "+5V"}
    stale = sorted(n for n in board_nets
                   if not AUTO_NET.match(n) and n.lstrip("/") not in declared)
    if stale:
        fails.append(f"board nets the schematic no longer declares: {stale}")
    print(f"[5] stale net names: {len(stale)}")

    # ---- 3. outline containment ----
    xs, ys = [], []
    for tag in ("gr_line", "gr_arc"):
        for g in sexp.findall(root, tag):
            if sexp.val(g, "layer") != "Edge.Cuts":
                continue
            for t in ("start", "mid", "end"):
                c = sexp.find(g, t)
                if c:
                    xs.append(c[1]); ys.append(c[2])
    bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
    print(f"[3] board: {bx1-bx0:.1f} x {by1-by0:.1f} mm")

    for fp in fps:
        ref = fp_ref(fp)
        for name, net, ptype, px0, py0, px1, py1 in pad_boxes(fp):
            if (px0 < bx0 + EDGE_CLEARANCE or px1 > bx1 - EDGE_CLEARANCE
                    or py0 < by0 + EDGE_CLEARANCE or py1 > by1 - EDGE_CLEARANCE):
                d = min(px0 - bx0, bx1 - px1, py0 - by0, by1 - py1)
                fails.append(f"pad {ref}.{name} is {d:+.2f} mm from the board edge "
                             f"(need {EDGE_CLEARANCE})")
        cb = courtyard_box(fp)
        if cb and (cb[0] < bx0 or cb[2] > bx1 or cb[1] < by0 or cb[3] > by1):
            over = max(bx0 - cb[0], cb[2] - bx1, by0 - cb[1], cb[3] - by1)
            warns.append(f"courtyard {ref} overhangs the outline by {over:.2f} mm")

    # ---- 4. courtyard overlaps ----
    boxes = [(fp_ref(f), courtyard_box(f)) for f in fps]
    boxes = [(r, b) for r, b in boxes if b]
    n_over = 0
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            ra, ba = boxes[i]
            rb, bb = boxes[j]
            if frozenset((ra, rb)) in ALLOWED_OVERLAP:
                continue
            o = overlap(ba, bb)
            if o:
                n_over += 1
                fails.append(f"courtyards overlap: {ra} / {rb} "
                             f"by {o[0]:.2f} x {o[1]:.2f} mm")
    print(f"[4] courtyard overlaps: {n_over}")

    # ---- 6. pours: mask-opening separation, and stitching that misses foreign pads ----
    MASK_SEP = 2.0
    zones = sexp.findall(root, "zone")
    vias = sexp.findall(root, "via")
    opens = []
    for g in sexp.findall(root, "gr_poly"):
        lay = str(sexp.val(g, "layer"))
        if not lay.endswith(".Mask"):
            continue
        p = sexp.find(g, "pts")
        xs = [xy[1] for xy in sexp.findall(p, "xy")]
        ys = [xy[2] for xy in sexp.findall(p, "xy")]
        opens.append((lay, min(xs), min(ys), max(xs), max(ys)))

    # Exposed copper is where molten solder can flow during bar assembly, so the
    # apertures -- not the copper -- are what must hold the 2 mm separation.
    bad_sep = 0
    for i in range(len(opens)):
        for j in range(i + 1, len(opens)):
            la, ax0, ay0, ax1, ay1 = opens[i]
            lb, bx0, by0, bx1, by1 = opens[j]
            if la != lb:
                continue
            dx = max(bx0 - ax1, ax0 - bx1, 0)
            dy = max(by0 - ay1, ay0 - by1, 0)
            d = math.hypot(dx, dy)
            if d < MASK_SEP - COURTYARD_TOL:
                bad_sep += 1
                fails.append(f"soldermask openings on {la} are only {d:.2f} mm apart "
                             f"(need {MASK_SEP})")

    # A stitching via dropped onto a pad of a different net is a dead short that no
    # amount of zone clearance will save you from.
    shorted = []
    for v in vias:
        at = sexp.find(v, "at")
        vnet = sexp.val(v, "net")
        r = (sexp.find(v, "size")[1]) / 2
        for fp in fps:
            for name, net, ptype, px0, py0, px1, py1 in pad_boxes(fp):
                if net == vnet:
                    continue
                if (px0 - r <= at[1] <= px1 + r) and (py0 - r <= at[2] <= py1 + r):
                    shorted.append(f"{fp_ref(fp)}.{name}({net}) x via({vnet})")
    if shorted:
        fails.append(f"stitching vias shorting foreign pads: {sorted(set(shorted))}")
    print(f"[6] pours: {len(zones)} zones, {len(opens)} mask openings "
          f"({bad_sep} too close), {len(vias)} stitching vias "
          f"({len(shorted)} shorting a foreign pad)")

    # ---- 7. nothing placed inside a soldermask opening ----
    # These apertures exist so a 10 AWG bar or a solder flood can be applied to the bus.
    # A component sitting in one gets swamped.  Placing R3 at u = 30 did exactly that and
    # it was only visible in a render, which is not a check.
    # The bus terminals themselves are meant to be in there -- the aperture exists to
    # expose the copper around J1/J3's pin 1 and pin 2 and U1's two tabs, so their
    # courtyards necessarily overlap it.  Everything else is an error.  Listed explicitly
    # rather than inferred from "has a pad on this net", because R3 taps LOAD+ too and
    # must still be caught.
    BUS_PARTS = {"J1", "J2", "J3", "J4", "U1"}
    inside = []
    for fp in fps:
        if fp_ref(fp) in BUS_PARTS:
            continue
        cb = courtyard_box(fp)
        if not cb:
            continue
        for lay, mx0, my0, mx1, my1 in opens:
            side = "F" if lay.startswith("F.") else "B"
            if side == "B":
                continue          # everything is on the front here
            ox = min(cb[2], mx1) - max(cb[0], mx0)
            oy = min(cb[3], my1) - max(cb[1], my0)
            if ox > COURTYARD_TOL and oy > COURTYARD_TOL:
                inside.append(f"{fp_ref(fp)} in the {lay} opening "
                              f"by {ox:.2f} x {oy:.2f} mm")
    if inside:
        fails.extend(sorted(set(inside)))
    print(f"[7] components inside a soldermask opening: {len(set(inside))}")

    # ---- 8. pad-to-pad clearance against the resolved netclass rules ----
    # A miniature DRC, and the reason it exists: a netclass clearance is applied to EVERY
    # pad on that net, so one over-generous number can quietly make a fine-pitch connector
    # unroutable.  GND_BUS at 0.5 mm did exactly that to J7 pin 2 on a 0.5 mm-pitch FPC
    # whose pads sit 0.2 mm apart by construction -- and nothing in this file noticed until
    # it was hit by hand in KiCad.
    #
    # KiCad resolves the clearance between two items as the LARGER of the constraints
    # applying to either, so that is what is checked here.
    import json
    pro = os.path.join(os.path.dirname(pcbpath), "Logging_Current_Meter.kicad_pro")
    clear, assign = {"Default": 0.2}, {}
    if os.path.exists(pro):
        d = json.load(open(pro))
        clear = {c["name"]: c["clearance"] for c in d["net_settings"]["classes"]}
        assign = {p["pattern"]: p["netclass"]
                  for p in d["net_settings"].get("netclass_patterns") or []}

    def required(na, nb):
        ca = clear.get(assign.get(na, "Default"), clear.get("Default", 0.2))
        cb = clear.get(assign.get(nb, "Default"), clear.get("Default", 0.2))
        return max(ca, cb)

    allpads = []
    for fp in fps:
        ref = fp_ref(fp)
        for name, net, ptype, x0, y0, x1, y1 in pad_boxes(fp):
            if net is None or ptype == "np_thru_hole":
                continue
            allpads.append((f"{ref}.{name}", net, x0, y0, x1, y1))

    viol = []
    for i in range(len(allpads)):
        ta, na, ax0, ay0, ax1, ay1 = allpads[i]
        for j in range(i + 1, len(allpads)):
            tb, nb, bx0, by0, bx1, by1 = allpads[j]
            if na == nb:
                continue
            dx = max(bx0 - ax1, ax0 - bx1, 0.0)
            dy = max(by0 - ay1, ay0 - by1, 0.0)
            gap = math.hypot(dx, dy)
            req = required(na, nb)
            if gap < req - COURTYARD_TOL:
                viol.append((gap, req, ta, na, tb, nb))

    viol.sort()
    for gap, req, ta, na, tb, nb in viol[:12]:
        fails.append(f"pad clearance {ta}({na}) to {tb}({nb}) is {gap:.3f} mm, "
                     f"rule requires {req:.3f}")
    if len(viol) > 12:
        fails.append(f"... and {len(viol) - 12} more pad-clearance violations")
    print(f"[8] pad-to-pad clearance violations: {len(viol)}")

    # ---- report ----
    print()
    for w in warns:
        print(f"  warn  {w}")
    for f in fails:
        print(f"  FAIL  {f}")
    print()
    if fails:
        print(f"{len(fails)} problem(s)")
        return 1
    print("clean" + (f", {len(warns)} warning(s)" if warns else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], "--dump" in sys.argv))
