#!/usr/bin/env python3
"""
Copper budget for the 150 A bus, computed from the AS-BUILT board rather than assumed.

Segment lengths are measured pad-edge to pad-edge out of the .kicad_pcb, so this cannot
drift away from the layout the way DESIGN.md rev A's hand-written table did.

Usage:  python3 copper_budget.py ../Logging_Current_Meter.kicad_pcb
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp
from verify_pcb import pad_boxes, fp_ref

RHO = 1.68e-8            # copper at 20 C, ohm-metre
OZ1, OZH = 0.035, 0.0175  # 1 oz outer, 0.5 oz inner -- JLCPCB 4-layer 1.6 mm standard
AWG10_AREA = 5.26e-6     # m^2, 2.588 mm solid
ACS770_R = 100e-6        # datasheet primary conductor
I_BURST, I_CONT = 150.0, 60.0

ORIGIN = (50.0, 50.0)


def pad_v(root, ref, padname):
    """(v_min, v_max) of a pad, board-local."""
    for fp in sexp.findall(root, "footprint"):
        if fp_ref(fp) != ref:
            continue
        box = [p for p in pad_boxes(fp) if str(p[0]) == padname]
        if box:
            lo = min(p[4] for p in box) - ORIGIN[1]
            hi = max(p[6] for p in box) - ORIGIN[1]
            return lo, hi
    raise SystemExit(f"{ref}.{padname} not found")


def main(path):
    root = sexp.loads(open(path).read())

    j1p = pad_v(root, "J1", "1")
    j3p = pad_v(root, "J3", "1")
    j1g = pad_v(root, "J1", "2")
    j3g = pad_v(root, "J3", "2")
    u1a = pad_v(root, "U1", "4")
    u1b = pad_v(root, "U1", "5")

    t_out = OZ1 * 2 + OZH        # F.Cu + B.Cu + In2 (positive) or + In1 plane (GND)

    segs = [
        ("PACK+  J1.1 -> U1 tab 4", u1a[0] - j1p[1], 10.0, t_out, True),
        ("U1 ACS770 conductor",     None,            None, None,  False),
        ("LOAD+  U1 tab 5 -> J3.1", j3p[0] - u1b[1], 10.0, t_out, True),
        ("GND    J1.2 -> J3.2",     j3g[0] - j1g[1], 15.0, t_out, True),
    ]

    print(f"{'segment':28s} {'L mm':>6s} {'W mm':>5s} "
          f"{'bare uR':>8s} {'+bar uR':>8s} {'P bare':>8s} {'P +bar':>8s}")
    tot_bare = tot_bar = 0.0
    for name, L, W, t, barable in segs:
        if L is None:
            r_bare = r_bar = ACS770_R
        else:
            r_bare = RHO * (L / 1000) / ((W / 1000) * (t / 1000))
            if barable:
                r_wire = RHO * (L / 1000) / AWG10_AREA
                r_bar = 1 / (1 / r_bare + 1 / r_wire)
            else:
                r_bar = r_bare
        pb, pw = I_BURST ** 2 * r_bare, I_BURST ** 2 * r_bar
        tot_bare += pb
        tot_bar += pw
        Ls = f"{L:6.2f}" if L is not None else f"{'-':>6s}"
        Ws = f"{W:5.1f}" if W is not None else f"{'-':>5s}"
        print(f"{name:28s} {Ls} {Ws} {r_bare*1e6:8.0f} {r_bar*1e6:8.0f} "
              f"{pb:7.2f}W {pw:7.2f}W")

    print(f"\n{'TOTAL at 150 A burst':28s} {'':6s} {'':5s} {'':8s} {'':8s} "
          f"{tot_bare:7.2f}W {tot_bar:7.2f}W")
    k = (I_CONT / I_BURST) ** 2
    print(f"{'at 60 A continuous':28s} {'':6s} {'':5s} {'':8s} {'':8s} "
          f"{tot_bare*k:7.2f}W {tot_bar*k:7.2f}W")
    print("\n'+bar' = a 10 AWG solid copper bar soldered along the exposed channel, in "
          "parallel with the PCB copper over the segment's full length.")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../Logging_Current_Meter.kicad_pcb")
