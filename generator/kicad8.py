#!/usr/bin/env python3
"""
KiCad 8 s-expression emitters (file format version 20231120).

Why this version:
  KiCad refuses to open a file whose format version is NEWER than the running
  application, and every KiCad release uses a different version stamp.  It is therefore
  impossible for one file to be "native" to both KiCad 9 and KiCad 10 at once.
  20231120 is the KiCad 8 format: comfortably below KiCad 9's ceiling, read natively by
  8, 9 and 10, and the best-documented of the modern formats.  KiCad 9/10 will offer to
  bump it to their own format the first time the project is saved.
"""

import uuid as _uuid
from sexp import Sym, dumps

SCH_VERSION = 20231120
LIB_VERSION = 20231120
GENERATOR = "lcm_gen"
GENERATOR_VERSION = "8.0"

S = Sym


def U():
    return str(_uuid.uuid4())


# --------------------------------------------------------------------------------------
#  small node builders
# --------------------------------------------------------------------------------------

def at(x, y, a=None):
    return [S("at"), float(x), float(y)] + ([int(a)] if a is not None else [])


def font(size=1.27, bold=False, italic=False):
    f = [S("font"), [S("size"), float(size), float(size)]]
    if bold:
        f.append([S("bold"), S("yes")])
    if italic:
        f.append([S("italic"), S("yes")])
    return f


def effects(size=1.27, hide=False, justify=None, bold=False):
    e = [S("effects"), font(size, bold=bold)]
    if justify:
        e.append([S("justify")] + [S(j) for j in justify])
    if hide:
        e.append(S("hide"))          # v8 uses the bare token; (hide yes) arrives at 20241004
    return e


def stroke(width=0.0, typ="default"):
    return [S("stroke"), [S("width"), float(width)], [S("type"), S(typ)]]


def fill(typ="none"):
    return [S("fill"), [S("type"), S(typ)]]


def prop(key, value, x, y, a=0, hide=False, justify=None, size=1.27):
    return [S("property"), key, value, at(x, y, a),
            effects(size=size, hide=hide, justify=justify)]


# --------------------------------------------------------------------------------------
#  symbol library
# --------------------------------------------------------------------------------------

class LibSymbol:
    def __init__(self, name, ref_prefix, description="", footprint="", datasheet="~",
                 is_power=False, hide_pin_names=False, hide_pin_numbers=False,
                 pin_name_offset=1.016, keywords=""):
        self.name = name
        self.ref_prefix = ref_prefix
        self.description = description
        self.footprint = footprint
        self.datasheet = datasheet
        self.is_power = is_power
        self.hide_pin_names = hide_pin_names
        self.hide_pin_numbers = hide_pin_numbers
        self.pin_name_offset = pin_name_offset
        self.keywords = keywords
        self.graphics = []
        self.pins = []          # (number, name, x, y, angle, length, etype, shape)

    # -- graphics ----------------------------------------------------------------------
    def rect(self, x1, y1, x2, y2, f="background", w=0.254):
        self.graphics.append([S("rectangle"),
                              [S("start"), float(x1), float(y1)],
                              [S("end"), float(x2), float(y2)],
                              stroke(w), fill(f)])

    def poly(self, pts, f="none", w=0.254):
        self.graphics.append([S("polyline"),
                              [S("pts")] + [[S("xy"), float(x), float(y)] for x, y in pts],
                              stroke(w), fill(f)])

    def circle(self, cx, cy, r, f="none", w=0.254):
        self.graphics.append([S("circle"),
                              [S("center"), float(cx), float(cy)],
                              [S("radius"), float(r)], stroke(w), fill(f)])

    def text(self, t, x, y, size=1.27):
        self.graphics.append([S("text"), t, at(x, y, 0), effects(size)])

    # -- pins --------------------------------------------------------------------------
    def pin(self, number, name, x, y, angle, length=2.54, etype="passive", shape="line"):
        self.pins.append((str(number), name, float(x), float(y), int(angle),
                          float(length), etype, shape))

    def geom(self):
        return {n: (x, y, a, nm, et) for n, nm, x, y, a, ln, et, sh in self.pins}

    # -- render ------------------------------------------------------------------------
    def node(self, nickname=None):
        full = f"{nickname}:{self.name}" if nickname else self.name
        n = [S("symbol"), full]
        if self.hide_pin_numbers:
            n.append([S("pin_numbers"), S("hide")])
        pn = [S("pin_names"), [S("offset"), float(self.pin_name_offset)]]
        if self.hide_pin_names:
            pn.append(S("hide"))
        n.append(pn)
        n.append([S("exclude_from_sim"), S("no")])
        n.append([S("in_bom"), S("yes")])
        n.append([S("on_board"), S("yes")])
        if self.is_power:
            n.append([S("power")])

        n.append(prop("Reference", self.ref_prefix, 0, 5.08,
                      hide=self.is_power, justify=["left"]))
        n.append(prop("Value", self.name, 0, -5.08, justify=["left"]))
        n.append(prop("Footprint", self.footprint, 0, -7.62, hide=True, justify=["left"]))
        n.append(prop("Datasheet", self.datasheet, 0, -10.16, hide=True, justify=["left"]))
        n.append(prop("Description", self.description, 0, -12.7, hide=True, justify=["left"]))
        if self.keywords:
            n.append(prop("ki_keywords", self.keywords, 0, -15.24, hide=True))

        body = [S("symbol"), f"{self.name}_0_1"] + self.graphics
        n.append(body)

        pinsec = [S("symbol"), f"{self.name}_1_1"]
        for num, nm, x, y, a, ln, et, sh in self.pins:
            pinsec.append([S("pin"), S(et), S(sh), at(x, y, a), [S("length"), ln],
                           [S("name"), nm, effects()],
                           [S("number"), num, effects()]])
        n.append(pinsec)
        return n


def render_lib(symbols):
    root = [S("kicad_symbol_lib"),
            [S("version"), LIB_VERSION],
            [S("generator"), GENERATOR],
            [S("generator_version"), GENERATOR_VERSION]]
    root += [s.node() for s in symbols]
    return dumps(root) + "\n"


# --------------------------------------------------------------------------------------
#  schematic
# --------------------------------------------------------------------------------------

class SchDoc:
    def __init__(self, paper="A2", project="project", title_block=None):
        self.uuid = U()
        self.paper = paper
        self.project = project
        self.title_block = title_block or {}
        self.lib_symbols = []
        self.items = []

    def wire(self, x1, y1, x2, y2):
        self.items.append([S("wire"),
                           [S("pts"), [S("xy"), float(x1), float(y1)],
                            [S("xy"), float(x2), float(y2)]],
                           stroke(0.0), [S("uuid"), U()]])

    def label(self, text, x, y, angle=0):
        self.items.append([S("label"), text, at(x, y, angle),
                           effects(justify=["left", "bottom"]), [S("uuid"), U()]])

    def text(self, t, x, y, size=1.27, bold=False):
        self.items.append([S("text"), t, at(x, y, 0),
                           effects(size=size, justify=["left"], bold=bold),
                           [S("uuid"), U()]])

    def rect(self, x1, y1, x2, y2, dash=True):
        self.items.append([S("rectangle"),
                           [S("start"), float(x1), float(y1)],
                           [S("end"), float(x2), float(y2)],
                           stroke(0.254, "dash" if dash else "default"),
                           fill("none"), [S("uuid"), U()]])

    def symbol(self, nickname, symname, ref, value, x, y, footprint="",
               dnp=False, in_bom=True, extra_fields=None, pin_numbers=()):
        n = [S("symbol"),
             [S("lib_id"), f"{nickname}:{symname}"],
             at(x, y, 0),
             [S("unit"), 1],
             [S("exclude_from_sim"), S("no")],
             [S("in_bom"), S("yes") if in_bom else S("no")],
             [S("on_board"), S("yes")],
             [S("dnp"), S("yes") if dnp else S("no")],
             [S("uuid"), U()]]
        hidden_ref = ref.startswith("#")
        n.append(prop("Reference", ref, x + 6.35, y - 2.54, hide=hidden_ref,
                      justify=["left"]))
        n.append(prop("Value", value, x + 6.35, y + 1.27, hide=hidden_ref,
                      justify=["left"]))
        n.append(prop("Footprint", footprint, x + 6.35, y + 3.81, hide=True,
                      justify=["left"]))
        n.append(prop("Datasheet", "~", x + 6.35, y + 6.35, hide=True, justify=["left"]))
        for k, v in (extra_fields or {}).items():
            n.append(prop(k, v, x + 6.35, y + 8.89, hide=True, justify=["left"]))
        for pnum in pin_numbers:
            n.append([S("pin"), str(pnum), [S("uuid"), U()]])
        n.append([S("instances"),
                  [S("project"), self.project,
                   [S("path"), f"/{self.uuid}",
                    [S("reference"), ref], [S("unit"), 1]]]])
        self.items.append(n)
        return n

    def render(self):
        root = [S("kicad_sch"),
                [S("version"), SCH_VERSION],
                [S("generator"), GENERATOR],
                [S("generator_version"), GENERATOR_VERSION],
                [S("uuid"), self.uuid],
                [S("paper"), self.paper]]

        tb = [S("title_block")]
        for k in ("title", "date", "rev", "company"):
            if self.title_block.get(k):
                tb.append([S(k), self.title_block[k]])
        for i, c in enumerate(self.title_block.get("comments", []), start=1):
            tb.append([S("comment"), i, c])
        if len(tb) > 1:
            root.append(tb)

        root.append([S("lib_symbols")] + self.lib_symbols)
        root += self.items
        root.append([S("sheet_instances"),
                     [S("path"), "/", [S("page"), "1"]]])
        # NOTE: no (embedded_fonts ...) here -- that token arrives with the KiCad 9
        # format and KiCad 8's parser rejects unknown tokens.
        return dumps(root) + "\n"
