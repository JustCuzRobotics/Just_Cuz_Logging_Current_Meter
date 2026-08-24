#!/usr/bin/env python3
"""
Schematic generator for the ESC Test Bench Mainboard.

Connectivity style: every symbol pin gets a short wire stub with a net label on its end.
No long point-to-point wires.  That is deliberate -- it is a normal, readable "signal
name" schematic style, and it makes the netlist a pure function of the label table below
rather than of wire routing geometry, which is what makes it machine-verifiable.

Output format is KiCad 8 (20231120); see kicad8.py for why.
"""

import json
import os
import sys

import gen_lib
from gen_lib import PIN_GEOM
from kicad8 import SchDoc, render_lib

G = 2.54
STUB = 5.08
PROJECT = "Logging_Current_Meter"
LIBNICK = "LCM"

# Footprints.  LCM: prefixed ones are project-specific and get created at the PCB stage.
FP_R = "Resistor_SMD:R_0805_2012Metric"
FP_C = "Capacitor_SMD:C_0805_2012Metric"
FP_CP = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
FP_FB = "Inductor_SMD:L_0805_2012Metric"
FP_SOT23 = "Package_TO_SOT_SMD:SOT-23"
FP_POT = "Potentiometer_THT:Potentiometer_Bourns_3362P_Vertical"
FP_SW = "Button_Switch_THT:SW_PUSH_6mm"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_HDR4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"
FP_HDR14 = "Connector_PinHeader_2.54mm:PinHeader_1x14_P2.54mm_Vertical"
FP_FPC14 = "LCM:FPC_1x14_P0.5mm_Horizontal"
FP_JMP3 = "Jumper:SolderJumper-3_P1.3mm_Open_TrianglePad1.0x1.5mm"
FP_ACS = "LCM:ACS770_CB_PFF"
FP_ZERO = "LCM:RP2040_Zero_Castellated"
FP_XT60 = "LCM:XT60_Vertical"
FP_XT30 = "LCM:XT30_Vertical"


def ongrid(v):
    return abs((v / 1.27) - round(v / 1.27)) < 1e-6


class Builder:
    def __init__(self):
        self.doc = SchDoc(paper="A2", project=PROJECT, title_block={
            "title": "ESC Test Bench Mainboard",
            "date": "2026-07-27",
            "rev": "A",
            "company": "Just Cuz Robotics",
            "comments": [
                "150 A / 60 V inline power meter and datalogger",
                "ACS770KCB-150U hall sensor + Waveshare RP2040-Zero",
                "Rev A -- schematic only, PCB layout not started",
                "Generated; check against DESIGN.md before ordering",
            ],
        })
        self.refs = {}
        self.netpins = {}
        self.errors = []
        self.used_syms = set()

    def place(self, symname, ref, value, x, y, footprint="", dnp=False, fields=None):
        if not (ongrid(x) and ongrid(y)):
            self.errors.append(f"{ref}: placement ({x},{y}) off the 1.27 mm grid")
        if ref in self.refs:
            self.errors.append(f"duplicate reference designator {ref}")
        if symname not in PIN_GEOM:
            raise KeyError(f"{ref}: unknown symbol {symname!r}")
        self.refs[ref] = symname
        self.used_syms.add(symname)
        self.doc.symbol(LIBNICK, symname, ref, value, x, y, footprint,
                        dnp=dnp, in_bom=not ref.startswith("#"),
                        extra_fields=fields, pin_numbers=PIN_GEOM[symname].keys())
        return _Part(self, symname, ref, x, y)

    def stub(self, part, pinnum, net):
        lx, ly, angle, pname, etype = PIN_GEOM[part.symname][str(pinnum)]
        px, py = part.x + lx, part.y - ly
        dx, dy = {0: (-1.0, 0.0), 90: (0.0, 1.0),
                  180: (1.0, 0.0), 270: (0.0, -1.0)}[angle % 360]
        ex, ey = px + dx * STUB, py + dy * STUB
        self.doc.wire(px, py, ex, ey)
        lang = {(-1.0, 0.0): 180, (1.0, 0.0): 0,
                (0.0, -1.0): 90, (0.0, 1.0): 270}[(dx, dy)]
        self.doc.label(net, ex, ey, lang)
        self.netpins.setdefault(net, []).append(f"{part.ref}.{pinnum}")

    def block(self, title, x1, y1, x2, y2):
        self.doc.rect(x1, y1, x2, y2)
        self.doc.text(title, x1 + 2.54, y1 + 5.08, size=2.54, bold=True)

    def note(self, t, x, y):
        self.doc.text(t, x, y)


class _Part:
    def __init__(self, b, symname, ref, x, y):
        self.b, self.symname, self.ref, self.x, self.y = b, symname, ref, x, y

    def n(self, **kw):
        for num, net in kw.items():
            self.b.stub(self, num.lstrip("p"), net)
        return self

    def byname(self, mapping):
        rev = {nm: num for num, (_, _, _, nm, _) in PIN_GEOM[self.symname].items()}
        for nm, net in mapping.items():
            if nm not in rev:
                raise KeyError(f"{self.ref}: {self.symname} has no pin named {nm!r}")
            self.b.stub(self, rev[nm], net)
        return self


class Grid:
    def __init__(self, x0, y0, xpitch=33.02, ypitch=30.48, cols=3):
        self.x0, self.y0, self.xp, self.yp, self.cols = x0, y0, xpitch, ypitch, cols
        self.i = 0

    def next(self):
        c, r = self.i % self.cols, self.i // self.cols
        self.i += 1
        return self.x0 + c * self.xp, self.y0 + r * self.yp


def build():
    b = Builder()

    # ==================================================================================
    #  A -- POWER PATH AND CURRENT SENSOR
    # ==================================================================================
    b.block("A  POWER PATH + CURRENT SENSOR", 17.78, 17.78, 198.12, 132.08)
    b.place("XT60", "J1", "XT60", 60.96, 43.18, FP_XT60,
            fields={"MPN": "XT60PW-M"}).n(**{"1": "PACK+", "2": "GND"})
    b.place("XT30", "J2", "XT30", 60.96, 68.58, FP_XT30,
            fields={"MPN": "XT30PW-M"}).n(**{"1": "PACK+", "2": "GND"})
    b.place("ACS770KCB-150U", "U1", "ACS770KCB-150U-PFF-T", 111.76, 60.96, FP_ACS,
            fields={"MPN": "ACS770KCB-150U-PFF-T"}) \
     .byname({"IP+": "PACK+", "IP-": "LOAD+", "VCC": "+5VS",
              "VIOUT": "I_RAW", "GND": "GND"})
    b.place("XT60", "J3", "XT60", 175.26, 43.18, FP_XT60,
            fields={"MPN": "XT60PW-F"}).n(**{"1": "LOAD+", "2": "GND"})
    b.place("XT30", "J4", "XT30", 175.26, 68.58, FP_XT30,
            fields={"MPN": "XT30PW-F"}).n(**{"1": "LOAD+", "2": "GND"})
    b.place("FERRITE", "FB1", "600R@100MHz 2A", 60.96, 111.76, FP_FB) \
     .n(**{"1": "+5V", "2": "+5VS"})
    b.place("C", "C1", "100n", 111.76, 111.76, FP_C).n(**{"1": "+5VS", "2": "GND"})
    b.place("C", "C2", "10u", 137.16, 111.76, FP_C).n(**{"1": "+5VS", "2": "GND"})
    b.place("PWR_FLAG", "#FLG1", "PWR_FLAG", 175.26, 111.76).n(**{"1": "+5VS"})

    b.note("Current flows through the POSITIVE leg only.", 22.86, 121.92)
    b.note("The negative bus is an unbroken pour and is the", 22.86, 125.73)
    b.note("ground reference for every measurement.", 22.86, 129.54)

    # ==================================================================================
    #  B -- CURRENT SENSE SCALING + 5 V RAIL SENSE
    # ==================================================================================
    b.block("B  CURRENT SCALING + RAIL SENSE", 205.74, 17.78, 320.04, 132.08)
    g = Grid(228.6, 45.72, 33.02, 33.02, 3)
    x, y = g.next(); b.place("C", "C3", "10n", x, y, FP_C).n(**{"1": "I_RAW", "2": "GND"})
    x, y = g.next(); b.place("R", "R1", "2k49 1%", x, y, FP_R).n(**{"1": "I_RAW", "2": "I_SENSE"})
    x, y = g.next(); b.place("R", "R2", "4k99 1%", x, y, FP_R).n(**{"1": "I_SENSE", "2": "GND"})
    x, y = g.next(); b.place("C", "C4", "10n", x, y, FP_C).n(**{"1": "I_SENSE", "2": "GND"})
    x, y = g.next(); b.place("R", "R5", "10k 1%", x, y, FP_R).n(**{"1": "+5VS", "2": "V5_SENSE"})
    x, y = g.next(); b.place("R", "R8", "10k 1%", x, y, FP_R).n(**{"1": "V5_SENSE", "2": "GND"})
    x, y = g.next(); b.place("C", "C6", "100n", x, y, FP_C).n(**{"1": "V5_SENSE", "2": "GND"})

    b.note("C3 = 10 nF is the ACS770 datasheet MAXIMUM load", 210.82, 112.9)
    b.note("capacitance on VIOUT. Do not increase it.", 210.82, 116.71)
    b.note("R1/R2 scale 0.5-4.5 V down to 0.33-3.00 V.", 210.82, 121.92)
    b.note("R5/R8 let firmware divide the supply back out:", 210.82, 125.73)
    b.note("I = [(ADC_I/ADC_5V) x (R5+R8)/R8 x (R1+R2)/R2 - 0.1] / 0.00534", 210.82, 129.54)

    # ==================================================================================
    #  C -- PACK VOLTAGE SENSE
    # ==================================================================================
    b.block("C  PACK VOLTAGE SENSE  (20.06:1)", 327.66, 17.78, 441.96, 132.08)
    g = Grid(350.52, 45.72, 33.02, 33.02, 3)
    x, y = g.next(); b.place("R", "R3", "95k3 1%", x, y, FP_R).n(**{"1": "LOAD+", "2": "VPACK_MID"})
    x, y = g.next(); b.place("R", "R4", "95k3 1%", x, y, FP_R).n(**{"1": "VPACK_MID", "2": "VPACK_TAP"})
    x, y = g.next(); b.place("R", "R6", "10k 1%", x, y, FP_R).n(**{"1": "VPACK_TAP", "2": "GND"})
    x, y = g.next(); b.place("R", "R7", "1k", x, y, FP_R).n(**{"1": "VPACK_TAP", "2": "V_PACK"})
    x, y = g.next(); b.place("C", "C5", "100n", x, y, FP_C).n(**{"1": "V_PACK", "2": "GND"})
    x, y = g.next(); b.place("BAT54S", "D1", "BAT54S", x, y, FP_SOT23) \
        .n(**{"1": "GND", "3": "V_PACK", "2": "+3V3"})

    b.note("Two 95k3 in series keeps each part inside its", 332.74, 112.9)
    b.note("150 V working rating and doubles the creepage.", 332.74, 116.71)
    b.note("60.0 V in -> 2.99 V at GP27. Divider drains 299 uA", 332.74, 121.92)
    b.note("from the pack continuously. R7 + D1 clamp the ADC pin", 332.74, 125.73)
    b.note("if R6 ever opens -- otherwise a cracked 0805 puts 60 V on GP27.", 332.74, 129.54)

    # ==================================================================================
    #  D -- THERMISTOR CHANNEL
    # ==================================================================================
    b.block("D  THERMISTOR  (100k NTC + trim pot)", 449.58, 17.78, 576.58, 132.08)
    g = Grid(472.44, 45.72, 33.02, 33.02, 3)
    x, y = g.next(); b.place("R", "R10", "0R", x, y, FP_R).n(**{"1": "+3V3", "2": "T_POT_TOP"})
    x, y = g.next(); b.place("POT_TRIM", "RV1", "100k", x, y, FP_POT) \
        .n(**{"1": "T_POT_TOP", "2": "T_NODE", "3": "T_NODE"})
    x, y = g.next(); b.place("CONN_1x2", "J5", "NTC 100k B3950", x, y, FP_HDR2) \
        .n(**{"1": "T_NODE", "2": "GND"})
    x, y = g.next(); b.place("R", "R9", "1k", x, y, FP_R).n(**{"1": "T_NODE", "2": "T_SENSE"})
    x, y = g.next(); b.place("C", "C7", "100n", x, y, FP_C).n(**{"1": "T_SENSE", "2": "GND"})

    b.note("Chain: +3V3 - R10 - RV1 - T_NODE - NTC - GND.", 454.66, 112.9)
    b.note("Ratiometric to the same 3V3 the ADC references,", 454.66, 116.71)
    b.note("so LDO drift cancels exactly -- no rail sense needed.", 454.66, 120.65)
    b.note("RV1 wiper tied to pin 3 = rheostat. Fit 91k at R10 to", 454.66, 125.73)
    b.note("make RV1 a fine trim instead of the whole upper leg.", 454.66, 129.54)

    # ==================================================================================
    #  E -- MICROCONTROLLER
    # ==================================================================================
    b.block("E  MICROCONTROLLER", 17.78, 139.7, 185.42, 297.18)
    b.place("RP2040_ZERO", "U2", "RP2040-Zero", 96.52, 205.74, FP_ZERO,
            fields={"MPN": "RP2040-Zero"}).byname({
        "5V": "+5V", "GND": "GND", "3V3": "+3V3",
        "GP29": "V5_SENSE", "GP28": "T_SENSE", "GP27": "V_PACK", "GP26": "I_SENSE",
        "GP15": "BTN2", "GP14": "BTN1",
        "GP0": "ESC_TELEM_MCU", "GP1": "ESC_SIG_MCU",
        "GP2": "MCU_SCK", "GP3": "MCU_MOSI", "GP4": "MCU_MISO",
        "GP5": "LCD_CS", "GP6": "LCD_RS", "GP7": "LCD_RST", "GP8": "MCU_LED",
        "GP9": "SD_CS", "GP10": "CTP_SDA", "GP11": "CTP_SCL",
        "GP12": "CTP_RST", "GP13": "CTP_INT",
    })
    b.note("USB-C must reach a board edge.", 22.86, 278.13)
    b.note("GP16 (onboard WS2812) and GP17-GP25 are not brought out on", 22.86, 283.21)
    b.note("the 23 castellations. All 20 usable GPIO are allocated --", 22.86, 287.02)
    b.note("there are no spares left on this pinout.", 22.86, 290.83)

    # ==================================================================================
    #  F -- DISPLAY, TOUCH AND microSD
    # ==================================================================================
    b.block("F  DISPLAY / TOUCH / microSD  (one 14-way bus)", 193.04, 139.7, 383.54, 297.18)
    disp = {"VCC": "DISP_VCC", "GND": "GND", "LCD_CS": "LCD_CS", "LCD_RST": "LCD_RST",
            "LCD_RS": "LCD_RS", "MOSI": "SPI_MOSI", "SCK": "SPI_SCK", "LED": "DISP_LED",
            "MISO": "SPI_MISO", "CTP_SCL": "CTP_SCL", "CTP_RST": "CTP_RST",
            "CTP_SDA": "CTP_SDA", "CTP_INT": "CTP_INT", "SD_CS": "SD_CS"}
    b.place("DISPLAY_14", "J6", "Display 1x14 hdr", 254.0, 190.5, FP_HDR14).byname(disp)
    b.place("DISPLAY_14", "J7", "Display 14p FPC 0.5mm", 342.9, 190.5, FP_FPC14).byname(disp)

    g = Grid(226.06, 254.0, 30.48, 27.94, 5)
    x, y = g.next(); b.place("R", "R11", "0R", x, y, FP_R).n(**{"1": "MCU_SCK", "2": "SPI_SCK"})
    x, y = g.next(); b.place("R", "R12", "0R", x, y, FP_R).n(**{"1": "MCU_MOSI", "2": "SPI_MOSI"})
    x, y = g.next(); b.place("R", "R13", "0R", x, y, FP_R).n(**{"1": "SPI_MISO", "2": "MCU_MISO"})
    x, y = g.next(); b.place("R", "R14", "0R", x, y, FP_R).n(**{"1": "MCU_LED", "2": "DISP_LED"})
    x, y = g.next(); b.place("JMP_3", "JP1", "5V / 3V3", x, y, FP_JMP3) \
        .n(**{"1": "+5V", "2": "DISP_VCC", "3": "+3V3"})

    b.note("J6 and J7 carry the SAME 14 signals -- fit one or the other.", 198.12, 278.13)
    b.note("JP1 default = bridge 1-2 (+5V). The module regulates to 3V3", 198.12, 283.21)
    b.note("on board and buffers its logic through a 74LVC245.", 198.12, 287.02)
    b.note("R11-R14 are 0R placeholders; fit 22R if the SPI edges ring.", 198.12, 290.83)

    # ==================================================================================
    #  G -- USER BUTTONS
    # ==================================================================================
    b.block("G  USER BUTTONS", 391.16, 139.7, 487.68, 248.92)
    g = Grid(414.02, 162.56, 38.1, 33.02, 2)
    x, y = g.next(); b.place("SW_PUSH", "SW1", "MODE", x, y, FP_SW).n(**{"1": "BTN1", "2": "GND"})
    x, y = g.next(); b.place("SW_PUSH", "SW2", "ZERO/TARE", x, y, FP_SW).n(**{"1": "BTN2", "2": "GND"})
    x, y = g.next(); b.place("C", "C12", "100n", x, y, FP_C).n(**{"1": "BTN1", "2": "GND"})
    x, y = g.next(); b.place("C", "C13", "100n", x, y, FP_C).n(**{"1": "BTN2", "2": "GND"})
    x, y = g.next(); b.place("R", "R15", "10k DNP", x, y, FP_R, dnp=True).n(**{"1": "+3V3", "2": "BTN1"})
    x, y = g.next(); b.place("R", "R16", "10k DNP", x, y, FP_R, dnp=True).n(**{"1": "+3V3", "2": "BTN2"})

    b.note("Internal pull-ups are used; R15/R16 are DNP backups.", 396.24, 240.03)
    b.note("SW2 tares the current reading at 0 A -- see DESIGN.md.", 396.24, 243.84)

    # ==================================================================================
    #  H -- ESC SIGNAL HEADER
    # ==================================================================================
    b.block("H  ESC SIGNAL / TELEMETRY", 495.3, 139.7, 576.58, 248.92)
    g = Grid(525.78, 162.56, 33.02, 33.02, 2)
    x, y = g.next(); b.place("CONN_1x4", "J8", "ESC SIG/TLM/5V/GND", x, y, FP_HDR4) \
        .n(**{"1": "ESC_SIG", "2": "ESC_TELEM", "3": "+5V", "4": "GND"})
    x, y = g.next(); b.place("R", "R17", "100R", x, y, FP_R).n(**{"1": "ESC_SIG_MCU", "2": "ESC_SIG"})
    x, y = g.next(); b.place("R", "R18", "1k", x, y, FP_R).n(**{"1": "ESC_TELEM", "2": "ESC_TELEM_MCU"})
    x, y = g.next(); b.place("BAT54S", "D2", "BAT54S", x, y, FP_SOT23) \
        .n(**{"1": "GND", "3": "ESC_TELEM_MCU", "2": "+3V3"})

    b.note("GP1 drives the ESC (servo PWM or PIO DShot).", 500.38, 232.41)
    b.note("GP0 reads telemetry through a PIO soft-UART, because", 500.38, 236.22)
    b.note("UART0 RX only lands on GP1/GP13 and both are taken.", 500.38, 240.03)
    b.note("R18 + D2 make a 5 V telemetry line safe for a 3V3 pin.", 500.38, 243.84)

    # ==================================================================================
    #  I -- RAILS AND DECOUPLING
    # ==================================================================================
    b.block("I  RAILS + DECOUPLING", 391.16, 256.54, 576.58, 358.14)
    g = Grid(414.02, 279.4, 33.02, 33.02, 4)
    x, y = g.next(); b.place("CP", "C10", "100u 16V", x, y, FP_CP).n(**{"1": "+5V", "2": "GND"})
    x, y = g.next(); b.place("C", "C11", "100n", x, y, FP_C).n(**{"1": "+5V", "2": "GND"})
    x, y = g.next(); b.place("C", "C8", "10u", x, y, FP_C).n(**{"1": "+3V3", "2": "GND"})
    x, y = g.next(); b.place("C", "C9", "100n", x, y, FP_C).n(**{"1": "+3V3", "2": "GND"})
    x, y = g.next(); b.place("GND", "#PWR1", "GND", x, y).n(**{"1": "GND"})
    x, y = g.next(); b.place("+3V3", "#PWR2", "+3V3", x, y).n(**{"1": "+3V3"})
    x, y = g.next(); b.place("+5V", "#PWR3", "+5V", x, y).n(**{"1": "+5V"})
    x, y = g.next(); b.place("PWR_FLAG", "#FLG2", "PWR_FLAG", x, y).n(**{"1": "GND"})

    b.note("All power is USB-derived: the RP2040-Zero ties VSYS", 396.24, 335.28)
    b.note("straight to USB VBUS. Nothing on this board runs", 396.24, 339.09)
    b.note("without the USB-C cable connected.", 396.24, 342.9)
    b.note("#FLG1/#FLG2 exist only to satisfy ERC.", 396.24, 347.98)

    # ==================================================================================
    #  J -- BUILD AND SAFETY NOTES
    # ==================================================================================
    b.block("J  BUILD AND SAFETY NOTES", 17.78, 304.8, 383.54, 400.05)
    for i, line in enumerate([
        "1.  RATINGS: 60 V DC max, 150 A burst. XT60 is a 60 A continuous connector --",
        "     150 A is a burst rating. Sustained high current will cook the connector",
        "     before it troubles the PCB. XT30 taps are for small packs only (30 A).",
        "",
        "2.  GROUND: logic ground is bonded to pack negative, so a tethered laptop sits",
        "     at pack negative too. Do not also attach a mains-grounded scope to the",
        "     motor side without checking for a ground loop first.",
        "",
        "3.  ACCURACY: the ACS770 itself is +/-2.4% of full scale, which dominates every",
        "     other error here. Offset (+/-10 mV) and post-150 A magnetic remanence",
        "     (up to 400 mA) are removed by taring at 0 A -- that is what SW2 is for.",
        "",
        "4.  V_PACK is tapped at the OUTPUT connector, so it reads the voltage the ESC",
        "     actually sees, including drop across the sensor's 100 uOhm conductor.",
        "",
        "5.  microSD is on the display module, sharing SPI0 with the LCD. Verify its",
        "     MISO tri-states when SD_CS is high before trusting logged data.",
        "",
        "6.  PCB LAYOUT IS NOT STARTED. Copper geometry for 150 A, XT connector spacing",
        "     and board outline are still open questions -- see DESIGN.md.",
    ]):
        b.note(line, 22.86, 314.96 + i * 3.81)

    return b


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    syms = gen_lib.build()
    with open(os.path.join(outdir, "LCM.kicad_sym"), "w", encoding="utf-8") as f:
        f.write(render_lib(syms))

    b = build()
    if b.errors:
        print("BUILD ERRORS:")
        for e in b.errors:
            print("  ", e)
        return 1

    byname = {s.name: s for s in syms}
    b.doc.lib_symbols = [byname[n].node(LIBNICK) for n in sorted(b.used_syms)]

    path = os.path.join(outdir, f"{PROJECT}.kicad_sch")
    with open(path, "w", encoding="utf-8") as f:
        f.write(b.doc.render())

    # ---- sym-lib-table so the LCM: nickname resolves when the project is opened -------
    with open(os.path.join(outdir, "sym-lib-table"), "w", encoding="utf-8") as f:
        f.write(
            "(sym_lib_table\n"
            "  (version 7)\n"
            '  (lib (name "LCM")(type "KiCad")(uri "${KIPRJMOD}/LCM.kicad_sym")'
            '(options "")(descr "ESC Test Bench Mainboard project symbols"))\n'
            ")\n")

    # ---- minimal project file; KiCad fills in every key it does not find --------------
    proj = {
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": ["LCM"]},
        "schematic": {
            "legacy_lib_dir": "",
            "legacy_lib_list": [],
            "net_format_name": "",
            "page_layout_descr_file": "",
            "spice_current_sheet_as_root": False,
            "spice_external_command": "spice \"%I\"",
            "spice_model_current_sheet_as_root": True,
            "spice_save_all_currents": False,
            "spice_save_all_voltages": False,
            "subpart_first_id": 65,
            "subpart_id_separator": 0,
        },
        "sheets": [[b.doc.uuid, "Root"]],
        "text_variables": {},
    }
    with open(os.path.join(outdir, f"{PROJECT}.kicad_pro"), "w", encoding="utf-8") as f:
        json.dump(proj, f, indent=2)

    print(f"wrote {path}")
    print(f"  format 20231120 (KiCad 8) -- opens in KiCad 8, 9 and 10")
    print(f"  {len(b.refs)} symbols, {len(b.netpins)} nets, "
          f"{sum(len(v) for v in b.netpins.values())} pin connections")
    print(f"  plus LCM.kicad_sym, sym-lib-table, {PROJECT}.kicad_pro")
    return 0


if __name__ == "__main__":
    sys.exit(main())
