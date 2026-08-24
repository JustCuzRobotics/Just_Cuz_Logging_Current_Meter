#!/usr/bin/env python3
"""
Project symbol library for the ESC Test Bench Mainboard  (LCM.kicad_sym).

Everything the schematic uses lives in this one library.  That is deliberate: a
self-contained library removes any possibility of KiCad re-linking a symbol to a stock
library whose pin geometry differs from what the schematic was generated against, which
would silently pull pins off their wire stubs.

Geometry conventions (KiCad symbol format):
  * In .kicad_sym, +Y is UP.
  * A pin's (at X Y ANGLE) is its ELECTRICAL CONNECTION POINT (the outer tip).  The pin
    line is drawn FROM that point IN the direction ANGLE, toward the body:
        0   -> body is to +X, pin sticks out LEFT
        180 -> body is to -X, pin sticks out RIGHT
        270 -> body is to -Y, pin sticks out UP   (library space)
        90  -> body is to +Y, pin sticks out DOWN (library space)
  * Placing a symbol on a sheet applies KiCad's DefaultTransform (1,0,0,-1):
        sheet_x = symbol_x + lib_x
        sheet_y = symbol_y - lib_y
    Schematic sheets use +Y DOWN, so that negation is exactly what makes a symbol look
    the same in the library editor and on the sheet.
"""

from kicad8 import LibSymbol, render_lib

MM = 2.54

# Display module interface.  Pin order is fixed by the Hosyond module silkscreen and is
# identical on the 2.54 mm header (P1) and the 0.5 mm 14-way FPC (P2).
DISPLAY_PINS = [
    ("1", "VCC"), ("2", "GND"), ("3", "LCD_CS"), ("4", "LCD_RST"),
    ("5", "LCD_RS"), ("6", "MOSI"), ("7", "SCK"), ("8", "LED"),
    ("9", "MISO"), ("10", "CTP_SCL"), ("11", "CTP_RST"), ("12", "CTP_SDA"),
    ("13", "CTP_INT"), ("14", "SD_CS"),
]

# RP2040-Zero castellated pads, read from the 5V pad with USB-C at the top:
# 9 down the left edge, 9 down the right edge, 5 across the bottom = 23 pads.
ZERO_LEFT = ["5V", "GND", "3V3", "GP29", "GP28", "GP27", "GP26", "GP15", "GP14"]
ZERO_RIGHT = ["GP0", "GP1", "GP2", "GP3", "GP4", "GP5", "GP6", "GP7", "GP8"]
ZERO_BOTTOM = ["GP9", "GP10", "GP11", "GP12", "GP13"]


def build():
    syms = []

    def add(s):
        syms.append(s)
        return s

    # ---------------- generic passives ------------------------------------------------
    s = add(LibSymbol("R", "R", "Resistor", hide_pin_names=True))
    s.rect(-1.016, -2.54, 1.016, 2.54, f="none")
    s.pin("1", "~", 0, 3.81, 270, 1.27)
    s.pin("2", "~", 0, -3.81, 90, 1.27)

    s = add(LibSymbol("C", "C", "Unpolarized capacitor", hide_pin_names=True))
    s.poly([(-2.032, 0.762), (2.032, 0.762)], w=0.508)
    s.poly([(-2.032, -0.762), (2.032, -0.762)], w=0.508)
    s.pin("1", "~", 0, 3.81, 270, 3.048)
    s.pin("2", "~", 0, -3.81, 90, 3.048)

    s = add(LibSymbol("CP", "C", "Polarized capacitor", hide_pin_names=True))
    s.poly([(-2.032, 0.762), (2.032, 0.762)], w=0.508)
    s.poly([(-2.032, -1.27), (2.032, -1.27)], w=0.508)
    s.poly([(-1.778, 2.286), (-0.762, 2.286)], w=0.2)
    s.poly([(-1.27, 2.794), (-1.27, 1.778)], w=0.2)
    s.pin("1", "+", 0, 3.81, 270, 3.048)
    s.pin("2", "-", 0, -3.81, 90, 3.048)

    s = add(LibSymbol("FERRITE", "FB", "Ferrite bead", hide_pin_names=True))
    s.rect(-2.54, -1.27, 2.54, 1.27, f="none")
    s.pin("1", "~", -5.08, 0, 0, 2.54)
    s.pin("2", "~", 5.08, 0, 180, 2.54)

    # Trim pot, used here as a rheostat (wiper tied to one end on the board).
    s = add(LibSymbol("POT_TRIM", "RV", "Trimmer potentiometer"))
    s.rect(-1.016, -2.54, 1.016, 2.54, f="none")
    s.poly([(3.302, 0), (1.778, 0.889), (1.778, -0.889), (3.302, 0)], f="outline")
    s.pin("1", "1", 0, 3.81, 270, 1.27)
    s.pin("2", "W", 6.35, 0, 180, 3.048)
    s.pin("3", "3", 0, -3.81, 90, 1.27)

    # BAT54S -- two schottky diodes in SERIES in one SOT-23:
    #   pin 1 = anode of D1
    #   pin 3 = cathode of D1 tied to anode of D2 (the common centre pin)
    #   pin 2 = cathode of D2
    # Wiring pin1 -> GND, pin3 -> signal, pin2 -> rail forms a two-sided input clamp:
    # signal above rail+Vf is shunted into the rail, below GND-Vf it is pulled up.
    s = add(LibSymbol("BAT54S", "D", "Dual schottky barrier diode, series, SOT-23"))
    s.poly([(-3.81, 1.27), (-3.81, -1.27), (-2.54, 0), (-3.81, 1.27)], f="outline")
    s.poly([(-2.54, 1.27), (-2.54, -1.27)])
    s.poly([(2.54, 1.27), (2.54, -1.27), (3.81, 0), (2.54, 1.27)], f="outline")
    s.poly([(3.81, 1.27), (3.81, -1.27)])
    s.poly([(-6.35, 0), (-3.81, 0)])
    s.poly([(-2.54, 0), (2.54, 0)])
    s.poly([(3.81, 0), (6.35, 0)])
    s.poly([(0, 0), (0, -5.08)])
    s.pin("1", "A1", -6.35, 0, 0, 0)
    s.pin("2", "K2", 6.35, 0, 180, 0)
    s.pin("3", "COM", 0, -5.08, 90, 0)

    s = add(LibSymbol("SW_PUSH", "SW", "Momentary tactile switch, SPST-NO",
                      hide_pin_names=True))
    s.poly([(-5.08, 0), (-2.032, 0)])
    s.poly([(5.08, 0), (2.032, 0)])
    s.poly([(-2.032, 1.016), (2.032, 1.016)])
    s.poly([(0, 1.016), (0, 2.54)])
    s.poly([(-1.27, 2.54), (1.27, 2.54)])
    s.circle(-2.032, 0, 0.508)
    s.circle(2.032, 0, 0.508)
    s.pin("1", "1", -7.62, 0, 0, 2.54)
    s.pin("2", "2", 7.62, 0, 180, 2.54)

    s = add(LibSymbol("JMP_3", "JP", "3-pad solder jumper, centre pad common"))
    s.circle(-2.54, 0, 0.762)
    s.circle(0, 0, 0.762)
    s.circle(2.54, 0, 0.762)
    s.poly([(-2.54, 1.778), (0, 1.778)], w=0.2)
    s.pin("1", "A", -5.08, 0, 0, 1.778)
    s.pin("2", "C", 0, -3.81, 90, 3.048)
    s.pin("3", "B", 5.08, 0, 180, 1.778)

    # Probe pad. Pin at the origin facing right, so a stub + net label attaches on the
    # right-hand side. Geometry must stay identical to the copy embedded in the
    # schematic's lib_symbols by add_testpoints.py.
    s = add(LibSymbol("TestPoint", "TP", "Probe pad", hide_pin_names=True,
                      hide_pin_numbers=True, pin_name_offset=0.254))
    s.circle(-3.175, 0, 0.635)
    s.pin("1", "1", 0, 0, 180, 2.54)

    # Mechanical mounting hole. Zero pins: it carries no net, but existing as a symbol
    # keeps it in the netlist so "Update PCB" cannot delete it as an extra footprint.
    s = add(LibSymbol("MountingHole", "H", "Mounting hole, mechanical only"))
    s.circle(0, 0, 1.27)
    s.circle(0, 0, 2.032)

    # ---------------- power symbols ---------------------------------------------------
    s = add(LibSymbol("GND", "#PWR", "Power symbol, ground", is_power=True,
                      hide_pin_names=True, hide_pin_numbers=True, pin_name_offset=0))
    s.poly([(0, 0), (0, -1.27)])
    s.poly([(-1.27, -1.27), (1.27, -1.27)])
    s.poly([(-0.762, -1.778), (0.762, -1.778)])
    s.poly([(-0.254, -2.286), (0.254, -2.286)])
    s.pin("1", "GND", 0, 0, 270, 0, etype="power_in")

    for rail in ("+3V3", "+5V"):
        s = add(LibSymbol(rail, "#PWR", f"Power symbol, {rail}", is_power=True,
                          hide_pin_names=True, hide_pin_numbers=True,
                          pin_name_offset=0))
        s.poly([(0, 0), (0, 1.27)])
        s.poly([(-0.762, 1.27), (0, 2.032), (0.762, 1.27)], f="outline")
        s.pin("1", rail, 0, 0, 90, 0, etype="power_in")

    s = add(LibSymbol("PWR_FLAG", "#FLG", "ERC power source flag", is_power=True,
                      hide_pin_names=True, hide_pin_numbers=True, pin_name_offset=0))
    s.poly([(0, 0), (0, 1.27), (-1.016, 1.905), (0, 2.54), (1.016, 1.905), (0, 1.27)])
    s.pin("1", "pwr", 0, 0, 90, 0, etype="power_out")

    # ---------------- connectors ------------------------------------------------------
    for nm, desc in (("XT60", "XT60 high-current connector, 2 pole"),
                     ("XT30", "XT30 high-current connector, 2 pole")):
        s = add(LibSymbol(nm, "J", desc))
        s.rect(-2.54, -5.08, 6.35, 5.08, f="none")
        s.text(nm, 1.905, 0, 1.27)
        s.pin("1", "+", -7.62, 2.54, 0, 5.08)
        s.pin("2", "-", -7.62, -2.54, 0, 5.08)

    s = add(LibSymbol("CONN_1x2", "J", "2-pin connector / plated solder pads"))
    s.rect(-1.27, -1.27, 2.54, 3.81, f="none")
    s.pin("1", "1", -5.08, 2.54, 0, 3.81)
    s.pin("2", "2", -5.08, 0, 0, 3.81)

    s = add(LibSymbol("CONN_1x4", "J", "4-pin header"))
    s.rect(-1.27, -6.35, 2.54, 3.81, f="none")
    for i in range(4):
        s.pin(str(i + 1), str(i + 1), -5.08, 2.54 - i * MM, 0, 3.81)

    # ---------------- display module --------------------------------------------------
    s = add(LibSymbol("DISPLAY_14", "J",
                      "Hosyond 3.5in 480x320 IPS, ST7796U + FT6336U + microSD, 14-way"))
    top = 16.51
    s.rect(-3.81, top - 13 * MM - 2.54, 12.7, top + 2.54, f="none")
    for num, nm in DISPLAY_PINS:
        s.pin(num, nm, -8.89, top - (int(num) - 1) * MM, 0, 5.08)

    # ---------------- ACS770 ----------------------------------------------------------
    # Terminal list from the ACS770xCB datasheet:
    #   1 VCC | 2 GND | 3 VIOUT | 4 IP+ | 5 IP-
    s = add(LibSymbol(
        "ACS770KCB-150U", "U",
        "Hall current sensor, 150 A unidirectional, 26.66 mV/A, 100 uOhm conductor",
        datasheet="https://www.allegromicro.com/-/media/files/datasheets/acs770-datasheet.pdf"))
    s.rect(-12.7, -15.24, 12.7, 15.24, f="none")
    s.text("ACS770", 0, 11.43, 1.778)
    s.text("150 A UNI", 0, 8.89, 1.27)
    s.poly([(-12.7, 7.62), (-6.35, 7.62), (-6.35, -7.62), (-12.7, -7.62)], w=0.508)
    s.poly([(-9.525, -7.62), (-9.525, -11.43)], w=0.2)
    s.circle(-9.525, -12.7, 1.27)
    s.pin("4", "IP+", -20.32, 7.62, 0, 7.62)
    s.pin("5", "IP-", -20.32, -7.62, 0, 7.62)
    s.pin("1", "VCC", 20.32, 7.62, 180, 7.62, etype="power_in")
    s.pin("3", "VIOUT", 20.32, 0, 180, 7.62, etype="output")
    s.pin("2", "GND", 20.32, -7.62, 180, 7.62, etype="power_in")

    # ---------------- RP2040-Zero -----------------------------------------------------
    # Pin NUMBERS 1..23 are this project's own convention; the project footprint is made
    # to match.  The pin NAMES are what matter electrically.
    s = add(LibSymbol("RP2040_ZERO", "U",
                      "Waveshare RP2040-Zero castellated module, 23 pads",
                      datasheet="https://www.waveshare.com/wiki/RP2040-Zero"))
    s.rect(-15.24, -35.56, 15.24, 27.94, f="none")
    s.text("RP2040-Zero", 0, 24.13, 1.778)
    n = 1
    for i, nm in enumerate(ZERO_LEFT):
        et = ("power_out" if nm in ("5V", "3V3")
              else "power_in" if nm == "GND" else "bidirectional")
        s.pin(str(n), nm, -20.32, 17.78 - i * MM, 0, 5.08, etype=et)
        n += 1
    for i, nm in enumerate(ZERO_RIGHT):
        s.pin(str(n), nm, 20.32, 17.78 - i * MM, 180, 5.08, etype="bidirectional")
        n += 1
    for i, nm in enumerate(ZERO_BOTTOM):
        s.pin(str(n), nm, -10.16 + i * 5.08, -40.64, 90, 5.08, etype="bidirectional")
        n += 1

    return syms


PIN_GEOM = {s.name: s.geom() for s in build()}
SYMS = {s.name: s for s in build()}


if __name__ == "__main__":
    import sys
    syms = build()
    path = sys.argv[1] if len(sys.argv) > 1 else "LCM.kicad_sym"
    with open(path, "w", encoding="utf-8") as f:
        f.write(render_lib(syms))
    print(f"wrote {path}: {len(syms)} symbols")
    for s in syms:
        print(f"   {s.name:22s} {len(s.pins):3d} pins")
