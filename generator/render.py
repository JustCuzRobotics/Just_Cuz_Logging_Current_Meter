#!/usr/bin/env python3
"""Render the generated schematic to PNG so the sheet can actually be looked at.

Readability check only -- verify.py handles correctness.  Approximates KiCad's renderer
closely enough to spot label collisions, overlapping symbols and text off the sheet.
"""
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle as MRect, Circle as MCircle
from matplotlib.lines import Line2D

from sexp import loads, find, findall

PAPER = {"A2": (594.0, 420.0), "A3": (420.0, 297.0), "A4": (297.0, 210.0)}
SYMC, WIREC, LBLC, TXTC = "#7a2020", "#006000", "#803030", "#1a3a7a"


def graphics_of(node):
    """Graphic items live in the (symbol "NAME_0_1" ...) sub-unit."""
    out = []
    for tag in ("rectangle", "polyline", "circle", "text", "arc"):
        out += [(tag, g) for g in findall(node, tag)]
    for sub in findall(node, "symbol"):
        out += graphics_of(sub)
    return out


def pins_of(node):
    out = []
    for p in findall(node, "pin"):
        pos, nm, num = find(p, "at"), find(p, "name"), find(p, "number")
        ln = find(p, "length")
        if pos and num:
            out.append((str(num[1]), nm[1] if nm else "", pos[1], pos[2],
                        int(pos[3]) if len(pos) > 3 else 0, ln[1] if ln else 2.54))
    for sub in findall(node, "symbol"):
        out += pins_of(sub)
    return out


def main(schpath, libpath, out):
    with open(schpath, encoding="utf-8") as f:
        sch = loads(f.read())
    with open(libpath, encoding="utf-8") as f:
        lib = {str(s[1]): s for s in findall(loads(f.read()), "symbol")}

    paper = find(sch, "paper")
    W, H = PAPER.get(str(paper[1]) if paper else "A2", (594.0, 420.0))

    fig, ax = plt.subplots(figsize=(W / 25.4, H / 25.4), dpi=150)
    ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.set_aspect("equal"); ax.axis("off")
    fig.patch.set_facecolor("white")
    ax.add_patch(MRect((10, 10), W - 20, H - 20, fill=False, ec="#555", lw=0.8))

    for r in findall(sch, "rectangle"):
        s, e = find(r, "start"), find(r, "end")
        ax.add_patch(MRect((s[1], s[2]), e[1] - s[1], e[2] - s[2],
                           fill=False, ec="#999", lw=0.6, ls=(0, (4, 3))))

    for w in findall(sch, "wire"):
        pts = findall(find(w, "pts"), "xy")
        ax.add_line(Line2D([p[1] for p in pts], [p[2] for p in pts],
                           color=WIREC, lw=0.7))

    for ss in findall(sch, "symbol"):
        libid = find(ss, "lib_id")
        if libid is None:
            continue
        # lib_id carries the nickname ("LCM:R"); the library file stores it bare ("R").
        full = str(libid[1])
        s = lib.get(full) or lib.get(full.split(":", 1)[-1])
        if not s:
            continue
        pos = find(ss, "at")
        ox, oy = pos[1], pos[2]
        hide_names = any(str(t) == "hide" for t in (find(s, "pin_names") or []))

        def T(x, y):
            return ox + x, oy - y

        for tag, g in graphics_of(s):
            if tag == "rectangle":
                a, b = find(g, "start"), find(g, "end")
                x1, y1 = T(a[1], a[2]); x2, y2 = T(b[1], b[2])
                ax.add_patch(MRect((min(x1, x2), min(y1, y2)),
                                   abs(x2 - x1), abs(y2 - y1),
                                   fc="#fffbe6", ec=SYMC, lw=0.7))
            elif tag == "polyline":
                pts = [T(p[1], p[2]) for p in findall(find(g, "pts"), "xy")]
                ax.add_line(Line2D([p[0] for p in pts], [p[1] for p in pts],
                                   color=SYMC, lw=0.8))
            elif tag == "circle":
                c, r = find(g, "center"), find(g, "radius")
                cx, cy = T(c[1], c[2])
                ax.add_patch(MCircle((cx, cy), r[1], fc="none", ec=SYMC, lw=0.7))
            elif tag == "text":
                p = find(g, "at")
                tx, ty = T(p[1], p[2])
                ax.text(tx, ty, g[1], color=SYMC, ha="center", va="center", fontsize=3.2)

        for num, nm, px_, py_, ang, ln in pins_of(s):
            px, py = T(px_, py_)
            dx, dy = {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}[ang % 360]
            ax.add_line(Line2D([px, px + dx * ln], [py, py + dy * ln],
                               color=SYMC, lw=0.6))
            if not hide_names and nm not in ("~", ""):
                ax.text(px + dx * (ln + 1.0), py + dy * (ln + 1.0), nm, color="#0a6",
                        fontsize=2.1,
                        ha="left" if dx > 0 else ("right" if dx < 0 else "center"),
                        va="center")

        props = {p[1]: p[2] for p in findall(ss, "property")}
        ref = props.get("Reference", "")
        if not ref.startswith("#"):
            ax.text(ox + 1.5, oy - 6.0, ref, color="#000", fontsize=3.0,
                    ha="left", va="bottom", weight="bold")
            ax.text(ox + 1.5, oy - 2.5, props.get("Value", ""), color="#444",
                    fontsize=2.6, ha="left", va="bottom")

    for lab in findall(sch, "label"):
        p = find(lab, "at")
        a = (int(p[3]) if len(p) > 3 else 0) % 360
        ha = {0: "left", 180: "right", 90: "left", 270: "right"}[a]
        ax.text(p[1], p[2], lab[1], color=LBLC, fontsize=2.7, ha=ha, va="center",
                rotation=0 if a in (0, 180) else 90, rotation_mode="anchor")

    for t in findall(sch, "text"):
        p = find(t, "at")
        eff = find(t, "effects")
        fnt = find(eff, "font") if eff else None
        sz = find(fnt, "size")[1] if fnt and find(fnt, "size") else 1.27
        ax.text(p[1], p[2], t[1], color=TXTC, fontsize=sz * 2.4, ha="left", va="center")

    fig.savefig(out, bbox_inches="tight", pad_inches=0.1)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "Logging_Current_Meter.kicad_sch",
         sys.argv[2] if len(sys.argv) > 2 else "LCM.kicad_sym",
         sys.argv[3] if len(sys.argv) > 3 else "sheet_preview.png")
