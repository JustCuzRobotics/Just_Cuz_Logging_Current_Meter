#!/usr/bin/env python3
"""Generate 1-bit bitmap fonts from Russo One TTF for logging_current_meter_FABLE.ino.

Output format per font:
  static const uint8_t  <NAME>_DATA[];            column-major bitmap bytes
  static const uint16_t <NAME>_OFF[N_GLYPHS];     offset into DATA per glyph
  static const uint8_t  <NAME>_W[N_GLYPHS];       glyph ink width (px)
  static const uint8_t  <NAME>_ADV[N_GLYPHS];     advance (px, incl. spacing)
  height, first char = 32, last char = 90
Each glyph: W columns x H rows, each column ceil(H/8) bytes, bit0 = top row.
Monospace digit font: every glyph advance forced to the max (tabular).
"""
from PIL import Image, ImageDraw, ImageFont

TTF = "/tmp/RussoOne.ttf"
CHARS = [chr(c) for c in range(32, 91)]   # space .. Z
NEEDED = set(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.<>/&%:")

def build(name, px, charset, mono=False, spacing=2):
    f = ImageFont.truetype(TTF, px)
    # find common vertical extent over the charset
    tops, bots = [], []
    for ch in charset:
        if ch == ' ': continue
        bbox = f.getbbox(ch)
        if bbox is None: continue
        tops.append(bbox[1]); bots.append(bbox[3])
    top, bot = min(tops), max(bots)
    H = bot - top
    bpc = (H + 7) // 8
    data, offs, ws, advs = [], [], [], []
    glyphs = {}
    for ch in CHARS:
        if ch not in charset:
            offs.append(0); ws.append(0); advs.append(0)
            continue
        if ch == ' ':
            offs.append(0); ws.append(0)
            advs.append(max(3, int(px*0.30)))
            continue
        bbox = f.getbbox(ch)
        w = bbox[2] - bbox[0]
        img = Image.new('1', (bbox[2] + 2, bot + 2), 0)
        ImageDraw.Draw(img).text((0, 0), ch, font=f, fill=1)
        pxl = img.load()
        offs.append(len(data)); ws.append(w)
        advs.append(w + spacing)
        cols = []
        for cx in range(w):
            colbytes = [0]*bpc
            for ry in range(H):
                if pxl[bbox[0]+cx, top+ry]:
                    colbytes[ry >> 3] |= (1 << (ry & 7))
            cols.append(colbytes)
            data.extend(colbytes)
        glyphs[ch] = (w, cols)
    if mono:
        m = max(a for a in advs if a) 
        advs = [m if a else 0 for a in advs]
    # emit C
    out = []
    out.append(f"/* {name}: Russo One @ {px}px -> {H}px tall, {len(data)} bytes */")
    out.append(f"#define {name}_H   {H}")
    out.append(f"#define {name}_BPC {bpc}")
    out.append(f"static const uint8_t {name}_DATA[] = {{")
    for i in range(0, len(data), 16):
        out.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",")
    out.append("};")
    out.append(f"static const uint16_t {name}_OFF[59] = {{ " +
               ",".join(str(o) for o in offs) + " };")
    out.append(f"static const uint8_t {name}_W[59] = {{ " +
               ",".join(str(w) for w in ws) + " };")
    out.append(f"static const uint8_t {name}_ADV[59] = {{ " +
               ",".join(str(a) for a in advs) + " };")
    return "\n".join(out), glyphs, H

def preview(glyphs, H, chars):
    for ch in chars:
        if ch not in glyphs: continue
        w, cols = glyphs[ch]
        print(f"--- {ch} ({w}x{H}) ---")
        for ry in range(H):
            print(''.join('#' if (cols[cx][ry>>3] >> (ry&7)) & 1 else '.' for cx in range(w)))

sections = []
s, g13, h13 = build("RUSSO13", 18, NEEDED); sections.append(s)
s, g16, h16 = build("RUSSO16", 22, NEEDED); sections.append(s)
s, g22, h22 = build("RUSSO22", 30, NEEDED); sections.append(s)
s, gBIG, hB = build("RUSSOBIG", 34, set("0123456789-.: ERC"), mono=True); sections.append(s)
open("/home/claude/fable_ui/fonts_generated.h", "w").write("\n\n".join(sections) + "\n")
print(f"heights: R13={h13} R16={h16} R22={h22} BIG={hB}")
import sys
preview(g16, h16, "AR5")
preview(gBIG, hB, "8.")
