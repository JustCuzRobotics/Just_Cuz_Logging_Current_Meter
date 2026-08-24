#!/usr/bin/env python3
"""
Render Logging_Current_Meter.kicad_pcb to SVG for eyeballing placement.

Deliberately reads the finished board file back off disk and re-derives every position
from it, rather than drawing the generator's in-memory placement table -- the point is
to see what is actually in the file, including anything the generator failed to move.

Usage:  python3 render_pcb.py ../Logging_Current_Meter.kicad_pcb out.svg
"""

import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp

SCALE = 8.0          # px per mm
PAD = 30.0           # px border

COL = {
    "edge":    "#111111",
    "crtyd":   "#8899aa",
    "pad_th":  "#c08a2e",
    "pad_smd": "#cc4433",
    "pad_np":  "#666666",
    "ref":     "#0b3d91",
    "fab":     "#b0b8c0",
    "bus_p":   "#d94f3d",
    "bus_n":   "#2f6fb5",
}

# Nets worth colouring so the current path reads at a glance.
NET_COLOUR = {"/PACK+": COL["bus_p"], "/LOAD+": COL["bus_p"], "GND": COL["bus_n"]}


def rot(x, y, deg):
    """KiCad footprint rotation: local (x, y) -> anchor-relative offset."""
    a = math.radians(deg)
    return (x * math.cos(a) + y * math.sin(a),
            -x * math.sin(a) + y * math.cos(a))


def main(pcbpath, outpath):
    root = sexp.loads(open(pcbpath).read())

    # ---- board extent from Edge.Cuts ----
    xs, ys = [], []
    for tag in ("gr_line", "gr_arc"):
        for g in sexp.findall(root, tag):
            if sexp.val(g, "layer") != "Edge.Cuts":
                continue
            for t in ("start", "mid", "end"):
                c = sexp.find(g, t)
                if c:
                    xs.append(c[1]); ys.append(c[2])
    if not xs:
        raise SystemExit("no Edge.Cuts outline in the board file")
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)

    W = (x1 - x0) * SCALE + 2 * PAD
    H = (y1 - y0) * SCALE + 2 * PAD

    def P(x, y):
        return ((x - x0) * SCALE + PAD, (y - y0) * SCALE + PAD)

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.1f} {H:.1f}" '
           f'width="{W:.1f}" height="{H:.1f}">',
           f'<rect width="{W:.1f}" height="{H:.1f}" fill="#fbfbf9"/>',
           '<g stroke-linecap="round">']

    # ---- outline ----
    for g in sexp.findall(root, "gr_line"):
        if sexp.val(g, "layer") != "Edge.Cuts":
            continue
        s, e = sexp.find(g, "start"), sexp.find(g, "end")
        a, b = P(s[1], s[2]), P(e[1], e[2])
        out.append(f'<line x1="{a[0]:.2f}" y1="{a[1]:.2f}" x2="{b[0]:.2f}" '
                   f'y2="{b[1]:.2f}" stroke="{COL["edge"]}" stroke-width="2"/>')
    for g in sexp.findall(root, "gr_arc"):
        if sexp.val(g, "layer") != "Edge.Cuts":
            continue
        s, m, e = sexp.find(g, "start"), sexp.find(g, "mid"), sexp.find(g, "end")
        a, b = P(s[1], s[2]), P(e[1], e[2])
        r = math.dist((s[1], s[2]), (m[1], m[2]))
        out.append(f'<path d="M {a[0]:.2f} {a[1]:.2f} A {r*SCALE:.2f} {r*SCALE:.2f} '
                   f'0 0 1 {b[0]:.2f} {b[1]:.2f}" fill="none" '
                   f'stroke="{COL["edge"]}" stroke-width="2"/>')

    # ---- zones, mask openings and stitching, under everything else ----
    ZCOL = {"GND": COL["bus_n"], "/PACK+": COL["bus_p"], "/LOAD+": COL["bus_p"]}
    for z in sexp.findall(root, "zone"):
        net = sexp.val(z, "net")
        lays = sexp.find(z, "layers")
        inner = lays and all("In" in str(l) for l in lays[1:])
        p = sexp.find(z, "polygon")
        pts = [P(xy[1], xy[2]) for xy in sexp.findall(sexp.find(p, "pts"), "xy")]
        d = " ".join(f"{'M' if i == 0 else 'L'} {x:.2f} {y:.2f}"
                     for i, (x, y) in enumerate(pts)) + " Z"
        col = ZCOL.get(net, COL["bus_n"])
        dash = ' stroke-dasharray="5 3"' if inner else ""
        out.append(f'<path d="{d}" fill="{col}" fill-opacity="{0.07 if inner else 0.16}"'
                   f' stroke="{col}" stroke-opacity="0.5" stroke-width="1"{dash}/>')

    for g in sexp.findall(root, "gr_poly"):
        lay = str(sexp.val(g, "layer"))
        if lay != "F.Mask":
            continue
        pts = [P(xy[1], xy[2]) for xy in sexp.findall(sexp.find(g, "pts"), "xy")]
        d = " ".join(f"{'M' if i == 0 else 'L'} {x:.2f} {y:.2f}"
                     for i, (x, y) in enumerate(pts)) + " Z"
        out.append(f'<path d="{d}" fill="none" stroke="#111" stroke-width="1.6" '
                   f'stroke-dasharray="6 3"/>')

    for v in sexp.findall(root, "via"):
        at = sexp.find(v, "at")
        c = P(at[1], at[2])
        r = sexp.find(v, "size")[1] / 2 * SCALE
        out.append(f'<circle cx="{c[0]:.2f}" cy="{c[1]:.2f}" r="{r:.2f}" '
                   f'fill="{ZCOL.get(sexp.val(v, "net"), "#888")}" fill-opacity="0.5"/>')

    # ---- footprints ----
    for fp in sexp.findall(root, "footprint"):
        at = sexp.find(fp, "at")
        fx, fy = at[1], at[2]
        fr = at[3] if len(at) > 3 else 0
        ref = next((p[2] for p in sexp.findall(fp, "property")
                    if p[1] == "Reference"), "?")

        # courtyard
        for g in fp[1:]:
            if not isinstance(g, list) or str(g[0]) not in (
                    "fp_line", "fp_rect", "fp_circle", "fp_poly", "fp_arc"):
                continue
            lay = sexp.val(g, "layer")
            if lay not in ("F.CrtYd", "B.CrtYd", "F.Fab", "B.Fab"):
                continue
            colour = COL["crtyd"] if "CrtYd" in str(lay) else COL["fab"]
            dash = '' if "CrtYd" in str(lay) else ' stroke-dasharray="3 2"'
            wid = 1.1 if "CrtYd" in str(lay) else 0.7

            def G(t):
                c = sexp.find(g, t)
                if not c:
                    return None
                dx, dy = rot(c[1], c[2], fr)
                return P(fx + dx, fy + dy)

            tag = str(g[0])
            if tag in ("fp_line",):
                a, b = G("start"), G("end")
                out.append(f'<line x1="{a[0]:.2f}" y1="{a[1]:.2f}" x2="{b[0]:.2f}" '
                           f'y2="{b[1]:.2f}" stroke="{colour}" '
                           f'stroke-width="{wid}"{dash}/>')
            elif tag == "fp_rect":
                a, b = G("start"), G("end")
                out.append(f'<rect x="{min(a[0],b[0]):.2f}" y="{min(a[1],b[1]):.2f}" '
                           f'width="{abs(b[0]-a[0]):.2f}" height="{abs(b[1]-a[1]):.2f}" '
                           f'fill="none" stroke="{colour}" '
                           f'stroke-width="{wid}"{dash}/>')
            elif tag == "fp_circle":
                c, e = sexp.find(g, "center"), sexp.find(g, "end")
                a = G("center")
                r = math.dist((c[1], c[2]), (e[1], e[2])) * SCALE
                out.append(f'<circle cx="{a[0]:.2f}" cy="{a[1]:.2f}" r="{r:.2f}" '
                           f'fill="none" stroke="{colour}" '
                           f'stroke-width="{wid}"{dash}/>')

        # pads
        for pd in sexp.findall(fp, "pad"):
            ptype = str(pd[2])
            pat = sexp.find(pd, "at")
            psz = sexp.find(pd, "size")
            prot = pat[3] if len(pat) > 3 else 0
            dx, dy = rot(pat[1], pat[2], fr)
            cx, cy = P(fx + dx, fy + dy)
            # pad's own rotation is absolute in KiCad; footprint rotation adds to it
            total = (fr + prot) % 180
            w, h = psz[1] * SCALE, psz[2] * SCALE
            if abs(total - 90) < 45:
                w, h = h, w
            net = sexp.val(pd, "net")
            if ptype == "np_thru_hole":
                fill = COL["pad_np"]
            elif ptype == "thru_hole":
                fill = COL["pad_th"]
            else:
                fill = COL["pad_smd"]
            fill = NET_COLOUR.get(net, fill)
            shape = str(pd[3])
            if shape == "circle":
                out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{w/2:.2f}" '
                           f'fill="{fill}" fill-opacity="0.85"/>')
            else:
                rx = min(w, h) / 2 if shape == "oval" else 1.0
                out.append(f'<rect x="{cx-w/2:.2f}" y="{cy-h/2:.2f}" width="{w:.2f}" '
                           f'height="{h:.2f}" rx="{rx:.2f}" fill="{fill}" '
                           f'fill-opacity="0.85"/>')

        # Silkscreen text, drawn at its real size so label legibility and collisions can
        # actually be judged from the render -- that is the whole reason the probe pads
        # are labelled with net names rather than designators.
        for g in sexp.findall(fp, "fp_text"):
            if sexp.val(g, "layer") != "F.SilkS":
                continue
            txt = str(g[2])
            tat = sexp.find(g, "at")
            eff = sexp.find(g, "effects")
            fnt = sexp.find(eff, "font") if eff else None
            size = sexp.find(fnt, "size")[1] if fnt and sexp.find(fnt, "size") else 1.0
            dx, dy = rot(tat[1], tat[2], fr)
            tp = P(fx + dx, fy + dy)
            out.append(f'<text x="{tp[0]:.2f}" y="{tp[1] + size*SCALE*0.36:.2f}" '
                       f'font-family="DejaVu Sans" font-size="{size*SCALE:.2f}" '
                       f'fill="#111" text-anchor="middle">{txt}</text>')

        a = P(fx, fy)
        # Halo behind the label, drawn as a real rect: cairosvg does not implement
        # paint-order, so a stroked text outline comes out as a smear.
        w = len(ref) * 5.2 + 3
        out.append(f'<rect x="{a[0]-w/2:.2f}" y="{a[1]-5:.2f}" width="{w:.2f}" '
                   f'height="10" fill="#fbfbf9" fill-opacity="0.82"/>')
        out.append(f'<text x="{a[0]:.2f}" y="{a[1]+3.1:.2f}" '
                   f'font-family="DejaVu Sans Mono" font-size="8.5" '
                   f'font-weight="bold" fill="{COL["ref"]}" '
                   f'text-anchor="middle">{ref}</text>')

    out.append("</g></svg>")
    open(outpath, "w").write("\n".join(out))
    print(f"wrote {outpath}  ({x1-x0:.1f} x {y1-y0:.1f} mm)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
