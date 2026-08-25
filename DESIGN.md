# ESC Test Bench Mainboard — Design Document

**Rev A — schematic complete, PCB not started.**
150 A / 60 V inline power meter and datalogger. A WM150-class meter with live logging,
a graphing display, and thermistor + ESC-signal channels.

---

## 1. Specification

| Parameter | Value | Notes |
|---|---|---|
| Max voltage | 60 V DC | 14S LiPo. Divider sized for exactly 60 V → 2.86 V |
| Max current | 150 A **burst** | See §7 — XT60 and the PCB are both burst-rated, not continuous |
| Continuous current | ~60 A | Limited by the XT60 connector, not the sensor |
| Current sensor | ACS770KCB-150U-PFF-T | Hall, galvanically isolated, 100 µΩ conductor |
| Supply | USB-C only | RP2040-Zero ties VSYS directly to VBUS |
| MCU | Waveshare RP2040-Zero | 23 castellated pads, 20 usable GPIO — all allocated |
| Display | Hosyond 3.5" 480×320 IPS | ST7796U + FT6336U cap touch + microSD, 14-way |
| Logging | microSD **and** USB CDC | SD slot is on the display module — no extra hardware |
| Channels | current, pack voltage, 5 V rail, 1× 100k NTC | plus ESC signal out + telemetry in |

---

## 2. Architecture

```
 XT60/XT30 in ──┬── ACS770 (IP+ → IP-) ──┬── XT60/XT30 out
   (+)          │                        │
                │                        └─ R3/R4/R6 ─ R7 ── V_PACK → GP27
                │                           (divider)  (fault limiter + D1 clamp)
                │
   (-) ─────────┴──────── unbroken negative bus = GND = measurement reference

  ACS770 VIOUT ── R1/R2 ── I_SENSE  → GP26
  +5V (VBUS) ──── R5/R8 ── V5_SENSE → GP29     (ratiometric correction)
  +3V3 ── RV1 ── T_NODE ── NTC ── GND, tap → GP28

  RP2040-Zero ── SPI0 ──┬── ST7796U LCD
                        ├── FT6336U touch (I2C1, separate)
                        └── microSD          all on the display's 14-way connector
```

The current sensor sits in the **positive** leg only. The negative leg is an unbroken
copper pour, which is both the current return and the voltage reference for every
measurement. `V_PACK` is tapped at the **output** connector so it reads what the ESC
actually sees, including the drop across the sensor's conductor.

---

## 3. Pin map — all 20 usable GPIO

| Pin | Net | Function |
|---|---|---|
| GP26 / ADC0 | `I_SENSE` | Current, via 0.662 divider |
| GP27 / ADC1 | `V_PACK` | Pack voltage, via 21:1 divider |
| GP28 / ADC2 | `T_SENSE` | 100k NTC divider |
| GP29 / ADC3 | `V5_SENSE` | 5 V rail ÷2 — the ratiometric reference |
| GP0 | `GP0` | **spare** — brought out to the J13 pad |
| GP1 | `ESC_SIG_MCU` | ESC drive out (servo PWM or PIO DShot) |
| GP2 / GP3 / GP4 | `MCU_SCK/MOSI/MISO` | SPI0, shared LCD + SD |
| GP5 | `LCD_CS` | |
| GP6 | `LCD_RS` | data/command |
| GP7 | `LCD_RST` | |
| GP8 | `MCU_LED` | backlight, PWM |
| GP9 | `SD_CS` | microSD on the display module |
| GP10 / GP11 | `CTP_SDA/SCL` | I2C1, FT6336U touch |
| GP12 / GP13 | `CTP_RST/INT` | |
| GP14 / GP15 | `BTN1/BTN2` | MODE / ZERO-TARE |

**There is exactly one spare pin: GP0.** It was the ESC telemetry input until that
channel was dropped (see below), and it is brought out to `J13`, a plated 1.0 mm pad, so
the freed pin stays reachable rather than stranded on a castellation.

GP16 is the onboard WS2812; GP17–GP25 exist on the RP2040 die but are not brought out to
the 23 castellations. They *are* on bottom-side solder pads, but reaching them would
force the module to be reflowed rather than hand-soldered, with joints you cannot see or
rework — so Rev A does not.

### ESC telemetry removed

`J8` was a 1x04 carrying signal, telemetry, +5V and GND. AM32 ESCs present a standard
3-wire servo lead — **signal, +5V, GND, in that order** — and the telemetry channel was
never likely to be used, so J8 is now a `LCM:CONN_1x3`. `R18` (the 1k telemetry series
resistor) and `D2` (its BAT54S clamp) went with it, and the nets `ESC_TELEM` and
`ESC_TELEM_MCU` no longer exist.

> **Pin 2 is deliberately not connected.** The pad exists only so the servo plug seats
> squarely. The RP2040-Zero ties VSYS straight to VBUS, so wiring an ESC's BEC to +5V
> here would back-feed the USB host whenever both were plugged in. A `no_connect` marker
> sits on it in the schematic — which is also what stops KiCad's ERC reporting a floating
> pin — and `verify.py` fails if that marker is ever deleted, rather than simply
> whitelisting the pin.

The stock `PinHeader_1x03_P2.54mm_Vertical` lives in the KiCad installation and cannot be
synthesised by the generator, so the footprint in `LCM.pretty` was **derived from the real
1x04 already on the board** — KiCad's own pad geometry, one pin shorter, with the silk,
courtyard and fab outlines each pulled in by one 2.54 mm pitch.

---

## 4. Analog design and calculations

### 4.1 Current channel

ACS770KCB-150U, from the datasheet:
sensitivity **26.66 mV/A** (26.02–27.30), quiescent output **0.5 V** at 0 A,
**ratiometric to VCC** (VRAT = 100 %), load ≥ **4.7 kΩ**, load capacitance ≤ **10 nF**.

```
VIOUT = (VCC/5) × (0.5 + 0.02666·I)        →  VIOUT/VCC = 0.1 + 0.005332·I
At VCC = 5.000 V, I = 150 A:  VIOUT = 4.499 V
```

Divider **R1 = 2.4k, R2 = 4.7k**:

```
G1 = R2/(R1+R2) = 4.7/7.1 = 0.661972
ADC pin at 150 A = 4.499 × 0.661972 = 2.978 V    (10 % headroom under 3.3 V)
ADC pin at   0 A = 0.500 × 0.661972 = 0.331 V
Load on VIOUT    = 7.1 kΩ  ≥ 4.7 kΩ minimum      OK
Source current   = 4.5 V / 7.1 kΩ = 0.63 mA ≤ 3 mA max   OK
Source Z at pin  = 2.4k ∥ 4.7k = 1.59 kΩ
C4 = 10 nF       → −3 dB at 10.0 kHz
```

> **Why E24 and not E96.** These were 2k49/4k99 in the first draft. Neither exists as a
> JLCPCB *Basic* part — 2k49 0805 is Extended with 19 k in stock. 2.4k and 4.7k are Basic
> with millions in stock and no feeder fee. The ratio moves from 0.66711 to 0.661972,
> which costs nothing: it is a fixed gain that the calibration in §6 removes anyway.

**C3 = 10 nF sits directly on VIOUT and is the datasheet maximum.** Do not increase it.
The sensor's quoted noise figure (6 mV, ±3σ ≈ 0.225 A) is specified at exactly this value.

Resolution: 3.3 V / 4096 = 0.806 mV/LSB; 1 A = 17.65 mV at the pin → **0.046 A/LSB**.

### 4.1.1 Future option: ACS772KCB-150U is a drop-in

The ACS770 is marked *not for new designs* but remains in stock, and **Rev A is being built
with it.** If it ever goes short, the **ACS772KCB-150U-PFF-T** replaces it with **no board
changes of any kind** — no footprint change, no component values, no firmware constants.

Checked against both datasheets (`ACS770-Datasheet.pdf`, ACS772 datasheet), every parameter
this design leans on is identical:

| | ACS770KCB-150U | ACS772KCB-150U |
|---|---|---|
| Package | 5-pin CB, PFF leadform | 5-pin CB, PFF leadform |
| Sensitivity, typ | **26.66 mV/A** | **26.66 × V_CC/5 mV/A** |
| Quiescent out, unidirectional | 0.5 V | V_CC/10 = 0.5 V |
| Ratiometric | V_RAT = 100 % | yes — ±0.15 % QVO, ±0.3 % Sens |
| R_LOAD minimum | 4.7 kΩ | 4.7 kΩ |
| C_LOAD maximum | 10 nF | 10 nF |
| Primary conductor | 100 µΩ | 100 µΩ |

So §4.1's transfer function is unchanged, R1/R2 stay (150 A → 2.978 V), the §4.2
ratiometric cancellation still works so R5/R8/C6 stay, C3 is still exactly at C_LOAD(max),
and the 7.1 kΩ load still clears the 4.7 kΩ minimum. Only `U1`'s value string and the LCSC
line change — and the footprint name `LCM:ACS770_CB_PFF` becomes a misnomer, though the
geometry is right for both.

**What you gain:** total output error **±2.4 % → ±1.5 %** max, and sensitivity tolerance
±2.4 % → ±1 %. §5 notes gain calibration already removes the sensitivity term, so the real
benefit is the temperature-drift part of the budget.

**What you do not gain: the 200 kHz bandwidth is unusable in this design.** C4 = 10 nF on a
1.59 kΩ source puts the anti-alias corner at 10 kHz, and firmware decimates to ~2 kHz. The
sensor's 120 → 200 kHz never reaches the ADC. Exploiting it would mean shrinking C4, which
wrecks the anti-aliasing for a 2 kHz sample rate. Don't.

**Two cautions, neither requiring a change here:**

- **The supply zener is gone.** The ACS770 has one at 6.5–7.5 V / 30 mA; the ACS772 has
  none. Irrelevant on this board — V_CC arrives via `+5V → FB1 → +5VS` with no series
  resistor, so the zener was never acting as a clamp. Abs-max V_CC actually *improves*,
  6 V → 6.5 V. A 5.5 V TVS on `+5VS` would be insurance against a USB hot-plug transient;
  there is no footprint for one and Rev A does not add it.
- **VIOUT abs max drops 25 V → 6.5 V.** Nothing on this board back-drives VIOUT — `I_RAW`
  reaches only C3, R1 and the TP4 probe pad. The one genuinely dangerous probe slip, TP4 to
  TP1 (`PACK+`, 60 V), was already fatal at 25 V, so nothing changes.

> **Two figures widely quoted for this comparison are wrong.** Abs-max V_CC does **not**
> drop — it rises 6 → 6.5 V. And creepage/clearance goes **6.9 → 6.8 mm**, not 7.25 → 6.8;
> the ACS770 datasheet gives 6.9 mm for both D_CL and D_CR. Certified working voltage is
> 475 V_RMS reinforced on both.

> **Noise cannot be compared directly from the datasheets.** The ACS770 specs 6 mV at
> **10 nF** on VIOUT; the ACS772 specs 3.4 mV RMS at **1 nF**. This board runs 10 nF, which
> is off the ACS772's specified condition, so whether it lands better or worse than 6 mV
> there is not knowable from the datasheets — measure it if you swap.

### 4.1.2 Sensor supply and decoupling

Asked often enough to be worth answering here: **where is the ACS770's decoupling
capacitor?** It is `C1`, and it is not on the raw +5 V rail — the sensor gets its own
ferrite-isolated island.

```
+5V ──┬── C10 100 uF ── FB1 ──┬── +5VS ── U1.1  (VCC)
      │                 600R  ├── C1  100 nF
      │              @100 MHz ├── C2  10 uF
      └── (MCU, display)      └── R5 ── V5_SENSE divider
```

The `+5VS` net is exactly `{U1.1, FB1.2, C1.1, C2.1, R5.1}`.

| Part | Role | Distance from U1 pin 1 |
|---|---|---|
| `FB1` 600R@100MHz, 2A | series isolation from MCU/display switching noise | 3.52 mm |
| `C1` 100 nF | HF decoupling | **8.01 mm** |
| `C2` 10 uF | local bulk | 12.51 mm |
| `C10` 100 uF | +5 V bulk, upstream of FB1 | 9.62 mm |

**`C3` is not supply decoupling** and the two get confused. `C3` (10 nF) sits on `I_RAW`
/ VIOUT, 3.55 mm from U1 pin 3. It is the datasheet's *output* load capacitance, at the
specified **maximum** — the sensor's quoted 6 mV noise figure is characterised at exactly
this value. Do not increase it, and do not remove it thinking it is a bypass cap.

**Why R5 taps `+5VS` and not `+5V`.** This is the part that actually makes the
measurement work. Section 4.2 recovers current from `ADC_I / ADC_5V`, so the reference
divider must see the *same* rail the sensor runs on. It does — `R5.1` is on `+5VS`,
downstream of the ferrite. Any drop or noise across `FB1` is therefore common to both
the sensor output and the reference measurement, and cancels algebraically. Tapping R5
off `+5V` instead would leave the ferrite's drop uncancelled and would be a real bug.
It is worth checking this survives any future re-layout.

> **Known weakness: C1 is 8 mm from the pin, and the ordering is backwards.**
> Conventional practice puts a 100 nF decoupler under ~5 mm, and puts the shunt cap
> nearest the pin with the series element further out. Here the physical order along the
> rail is U1 → FB1 → C1 → C2, so the closest part to VCC is the series ferrite.
> Electrically the topology is correct (ferrite in series, caps shunting on the load
> side); only the placement fails to reflect it.
>
> It works regardless, for three reasons worth stating rather than assuming:
> the ACS770 is not a switching load (~14 mA steady, no fast transient demand); the
> bandwidth is deliberately discarded downstream (`C4` puts the anti-alias corner at
> 10 kHz against the sensor's 120 kHz, and firmware decimates to ~2 kHz), so HF supply
> behaviour never reaches the ADC; and the ratiometric architecture above cancels supply
> variation as common mode rather than relying on the rail being quiet.
>
> **Rev B: swap `C1` and `FB1`.** A series element's position along the run barely
> matters; a decoupling capacitor's does. That puts C1 about 2.5 mm from the pin at no
> cost.

### 4.2 The ratiometric trick (why R5/R8 exist)

The ACS770 output scales with its own supply, and that supply is raw USB VBUS — which
sags with cable resistance and, worse, is modulated by the display backlight PWM. Rather
than fight this with a regulator, measure it:

```
ADC_I  = VIOUT × G1 / VREF × 4095
ADC_5V = VCC   × G2 / VREF × 4095 ,   G2 = R8/(R5+R8) = 0.5

ADC_I / ADC_5V = (VIOUT/VCC) × (G1/G2) = (0.1 + 0.005332·I) × G1/G2

    I = [ (ADC_I / ADC_5V) × (G2/G1) − 0.1 ] / 0.005332          G2/G1 = 0.755319
```

**Both `VCC` and `VREF` cancel out of that expression.** Accuracy then depends only on
two resistor ratios and the sensor's own spec — not on the USB rail, not on the 3.3 V
LDO, and not on backlight ripple.

### 4.3 Pack voltage

**R3 = R4 = 100k, R6 = 10k**, split into two series parts so each stays inside its 150 V
working rating and the creepage doubles.

```
ratio = 10 / (100 + 100 + 10) = 10/210 = 0.047619      (exactly 21:1)
60.0 V → 2.857 V          resolution 16.9 mV/LSB
pack drain = 60 V / 210 kΩ = 286 µA continuous (17 mW) — negligible, but always on
source Z = 200k ∥ 10k = 9.52 kΩ  (+ R7 1k)
C5 = 100 nF → −3 dB at ~150 Hz, and it makes the source stiff enough for the SAR ADC
```

> Also moved off E96 for the same reason: 95k3 is Extended with 3.5 k in stock, 100k is
> Basic with 6.4 M. 21:1 is a rounder number to work with in firmware, and the extra
> headroom under 3.3 V is welcome.

### The five nets, and what R7 is actually for

`PACK+` and `V_PACK` are not the same net, and the divider does not tap `PACK+` at all:

| Net | Pads | V at 60 V in |
|---|---|---|
| `PACK+` | J1.1, J2.1, U1.4, TP1.1 | 60.00 |
| `LOAD+` | J3.1, J4.1, U1.5, **R3.1**, TP2.1 | 60.00 |
| `VPACK_MID` | R3.2, R4.1 | 31.43 |
| `VPACK_TAP` | R4.2, R6.1, R7.1, TP7.1 | 2.857 |
| `V_PACK` | R7.2, C5.1, **D1.3**, U2.6, TP8.1 | 2.857 |

`R3.1` sits on **LOAD+**, the sensor's output side — that is the §2 choice to read what the
ESC actually sees. `VPACK_TAP` and `V_PACK` are the same DC potential, because no current
flows into an ADC pin and so nothing drops across R7; they are separate nets only because
a component sits between them. **D1 therefore never sees 60 V** — pin 3 is on the 2.86 V
side, clamping the ADC pin to the rails.

> **Correction to rev A.** This paragraph used to read *"a cracked R6 puts the full pack
> voltage onto GP27 and destroys the RP2040"*. That is wrong. If R6 goes **open**,
> `VPACK_TAP` does rise toward 60 V, but through R3 + R4 = 200 kΩ — so the clamp draws
> (60 − 3.6) / 201 kΩ ≈ **280 µA**, which the RP2040's own input protection shrugs off.
> An open R6 is a benign failure that reads full scale, not a destructive one. R6 **short**
> is equally benign: the channel reads zero.

**What R7 is really for** is any fault that puts a *low-impedance* path from the 60 V bus
onto `VPACK_TAP` — both upper legs bridged, a 0 Ω or wrong-value part fitted at R3 or R4,
or a slipped probe lead. With 60 V driven straight onto the node, R7 holds the clamp
current to (60 − 3.6) / 1 kΩ ≈ **56 mA**, inside the BAT54S's 200 mA rating. Without R7
that fault is limited only by the diode's forward resistance, and it destroys D1 and the
GP27 pad with it. Note this is a live risk in layout as well as assembly: **R3 sits about
1 mm from the exposed LOAD+ pour**, so solder splash during bar assembly is a credible
route to exactly that fault.

D1 is a **BAT54S — the *series* variant**, pin 3 the common node. With GND on pin 1 and
+3V3 on pin 2 that gives a diode from GND up to `V_PACK` and another from `V_PACK` up to
+3V3, clamping excursions in both directions. The `A`/`C`/single variants of the BAT54
would *not* work here: swap in a BAT54C by mistake and the clamp does nothing.

**Both parts stay** — for a better reason than rev A gave.

This channel is **not** ratiometric — it is referenced to GND, so the 3.3 V LDO tolerance
(±2 % on a typical ME6211) is a direct gain error. **A one-point voltage calibration is
mandatory**; see §6.

### 4.4 Thermistor

```
+3V3 ── R10 (0R) ── RV1 (100k trim) ── T_NODE ── NTC 100k ── GND      tap → R9 → GP28
```

NTC on the bottom leg deliberately: one thermistor lead is at ground (quieter on a long
run to a motor), and both failure modes are unmistakable — an open lead reads 3.3 V, a
short to chassis reads 0 V.

```
V_node / 3V3 = R_ntc / (R_pot + R_ntc)          — ratiometric, VREF cancels exactly
25 °C, R_ntc = 100k, pot = 100k :  1.650 V
 0 °C, R_ntc = 336k             :  2.543 V
100 °C, R_ntc = 6.99k           :  0.216 V      (B = 3950)

Recover:  R_ntc = R_pot · x/(1−x),  x = ADC/4095
          T = 1 / ( 1/298.15 + ln(R_ntc/100k)/3950 )
```

Worst-case source impedance is 50 kΩ (when R_ntc = R_pot), far too high for the RP2040's
SAR input on its own — **C7 = 100 nF at the pin is what makes this work**, by supplying
the sampling charge locally.

RV1 is wired as a rheostat (wiper tied to pin 3). `R10` is a 0 Ω placeholder: fit **91k**
there and RV1 becomes a fine trim over 91–191 kΩ instead of setting the whole upper leg,
which is considerably more stable and easier to set.

> ### RV1 footprint — was the wrong 3362 variant, now corrected
>
> The part is **JIERR 3362P-1-104, LCSC `C48997913`** — 100 kΩ, 0.5 W, 7 × 6.8 mm,
> ~$0.19 @ 5.
>
> `LCM:Potentiometer_Bourns_3362P_Vertical` was originally built with **all three pads in
> a row** at (−2.54, 0), (0, 0), (+2.54, 0). That is the plain **`3362`**, not the
> **`3362P`**. The vendor EasyEDA model in `easyECADDownloader/` settles it:
>
> ```
> pad 1  (-2.54,  1.27)
> pad 2  ( 0.00, -1.27)     <- offset 2.54 mm perpendicular, centred
> pad 3  ( 2.54,  1.27)
> ```
>
> Pins 1 and 3 are **5.08 mm** apart with pin 2 offset **2.54 mm** — a triangle. As
> originally drawn, pin 2 had nowhere to go and the part would not have fitted.
>
> The two variants are easy to confuse because the 3362P's *side* view reads "2.5 + 2.5",
> which is the triangle projected flat and looks identical to the in-line 3362. **Only the
> top view distinguishes them.** The footprint is now rebuilt from the vendor pad data;
> 0.9 mm drill retained for hand insertion on a ⌀0.5 mm lead.
>
> **To apply:** the library name is unchanged, so KiCad's *Update Footprints from Library*
> fixes RV1 in place. Note **all three pads move 1.27 mm**, not just pin 2, so all 9 track
> endpoints on RV1 break — `T_NODE` and `T_POT_TOP` need re-routing. Verified against a
> copy of the routed board beforehand: 0 courtyard overlaps and 0 clearance violations at
> RV1's current position, so nothing else has to move.

---

## 5. Accuracy budget — read this before trusting a number

### Current

| Source | Contribution |
|---|---|
| Sensor sensitivity error | ±2.4 % **of reading** |
| Sensor nonlinearity | ±1 % **of full scale** = ±1.5 A |
| Sensor noise (10 nF on VIOUT) | ±0.225 A (±3σ) |
| Offset ±10 mV + magnetic remanence ≤400 mA | **removed by taring** |
| Divider ratio, 1 % resistors | ±1.1 % — **removed by gain calibration** |

Worst case after tare and gain calibration:

| Current | Absolute error | Relative |
|---|---|---|
| 150 A | ±5.3 A | 3.5 % |
| 50 A | ±2.9 A | 5.8 % |
| 10 A | ±1.9 A | **19 %** |

**The nonlinearity term is fixed at ±1.5 A regardless of reading**, so this instrument is
honest between roughly 30 A and 150 A and increasingly meaningless below ~20 A. That is
inherent to putting a 150 A hall sensor on a small current — not a flaw in this board. If
you need good 5–20 A resolution, that wants a second, smaller sensor or a shunt.

For *comparative* ESC testing — same board, same session, A vs B — the systematic terms
cancel and repeatability is far better than the absolute numbers above.

### Voltage
±2.2 % uncalibrated (dominated by the 3.3 V LDO), **~±0.3 % after a one-point
calibration**, thereafter limited by LDO tempco (~100 ppm/°C) and resistor drift.

### Temperature
Ratiometric, so no reference error. Realistically ±2 °C after trimming RV1, limited by
NTC tolerance and B-value spread.

> **Consider 0.1 % resistors for R1, R2, R5, R8.** They cost about five cents more and
> remove the ±1.1 % divider term before you calibrate anything.

---

## 6. Calibration procedure

1. **Zero / tare.** USB only, nothing in the current path. Hold **SW2**. Firmware averages
   4096 samples and stores the offset. Repeat after any run that saw large current —
   the ACS770 retains up to 400 mA of magnetic offset after a 150 A excursion.
2. **Voltage gain.** Apply a known DC voltage (bench supply, verified with a good DMM) to
   the input. Adjust `v_gain` until the reading matches. One point through the origin.
3. **Current gain.** Pass a known current through the board — resistive load plus a clamp
   meter, or a calibrated shunt. Adjust `i_gain`. Do this near the top of the range where
   the percentage error of the reference matters least.
4. **Temperature.** Ice bath (0 °C) or a reference probe. Trim RV1 until it agrees, or
   measure the pot with a DMM and enter the value as `t_pot_ohms`.

Store all four constants in flash (LittleFS, or a dedicated sector).

---

## 7. The 150 A problem — quantified

This is the main thing to settle before layout.

**Connector.** XT60 is a 60 A continuous connector with roughly a 180 A burst rating.
150 A through it is fine for the seconds-long pulls typical of ESC testing and will
overheat on sustained load. The connector, not the board, is the limit. XT30 taps are for
small packs only (30 A).

**PCB copper.** IPC-2221 for external traces, 2 oz, 20 °C rise:

| Current | Required trace width (2 oz) |
|---|---|
| 60 A | ~27 mm |
| 150 A | ~97 mm |

Continuous 150 A through PCB copper is not practical at any sane board size. What *is*
practical is a short, wide, low-resistance bus that survives bursts on thermal mass:

```
25 mm long × 20 mm wide, 2 oz on both layers stitched (≈4 oz effective, t ≈ 0.14 mm):
  R = ρL/(Wt) = 1.68e-8 × 0.025 / (0.020 × 0.00014) ≈ 150 µΩ
  at 150 A → 3.4 W in the bus
  plus the ACS770's own 100 µΩ conductor → 2.25 W
  ≈ 5.6 W total during a burst
```

Five-plus watts is fine for a ten-second pull into a few grams of copper, and completely
unacceptable continuously. Mitigations for layout: keep the path as short as physically
possible, flood the bus with exposed (soldermask-free) copper so it can be built up with
solder or a copper braid, and stitch both layers with a dense via field.

---

## 8. PCB parameters — decided

| Parameter | Decision |
|---|---|
| Board size | **95 × 62 mm** — see "The 50 mm error" below |
| Layers | **4**, 1 oz outer / 0.5 oz inner (JLCPCB standard) |
| Connectors | XT60 and XT30 side by side, **one pair per 95 mm edge** |
| Current direction | **Across the 50 mm dimension**, top edge to bottom edge |
| ACS770 orientation | **Rotated 90°** so its terminals align with the current direction |
| Bus construction | Exposed copper (no soldermask) on L1 and L4, via-stitched, **10 AWG solid copper soldered along each** |
| Display | FPC only (J7). Module mounts to the **enclosure**, not the board |
| Expansion | Test point on every net, spare 3V3 / 5V / GND pads |
| Mounting | H1–H4, M3, unplated |

### Why the current crosses the short dimension

The ACS770's two current terminals sit **side by side, 10 mm apart** — not end to end.
The sensor is therefore only ~11 mm deep in the current direction, with its 20 mm body
projecting sideways where the signal pins are. Stacking the connectors top and bottom
with the rotated sensor between them gives:

### The 50 mm error

Rev A costed this budget with "XT60 footprint depth 9.1 mm". **That is the connector's
pad extent, not its body.** The F.Fab outline is 18.2 mm (XT60PW-M) and 17.2 mm
(XT60PW-F), and the vendor STEP models agree: the XT60PW occupies 15.5 × 18.2 mm of
board area, stands 8 mm tall, and mates horizontally. The body sits on the board.

The ACS770 was also costed at "10.8 mm deep in the current direction". Rotated 90° its
courtyard is **19.5 mm** in that direction — the 10 mm tab pitch plus two 9 mm-wide tab
slots and their stitching-via rings.

Corrected, with the real numbers:

```
board edge margin        1.5
J1 XT60-M body          18.2
bus gap                  2.0
U1 ACS770 courtyard     19.5
bus gap                  2.8
J3 XT60-F body          17.2
board edge margin        0.8
─────────────────────────────
minimum height          62.0 mm
current path            ~28 mm       J1 pad v=17.85 -> U1 tabs -> J3 pad v=45.65
```

The current path is unchanged at ~28 mm, which is the number that actually mattered —
only the dead space around it grew. **95 × 62 mm is still inside JLCPCB's ≤ 100 × 100 mm
bracket, so the change costs nothing but 12 mm of enclosure depth.**

### Copper budget at 150 A — as built

Measured from the finished board by `generator/copper_budget.py`, which reads the pad
geometry out of the `.kicad_pcb` rather than trusting a hand-written table. Segment
lengths are pad edge to pad edge. Copper is 1 oz outer on F.Cu + B.Cu plus the 0.5 oz
inner (In2 for the positive channels, the In1 plane for GND) = 0.0875 mm effective.

| Segment | Length | Bare PCB | + 10 AWG bar | P bare | P + bar |
|---|---|---|---|---|---|
| PACK+ J1.1 → U1 tab 4 | 2.29 mm | 44 µΩ | **6 µΩ** | 0.99 W | 0.14 W |
| ACS770 conductor | — | 100 µΩ | 100 µΩ | 2.25 W | 2.25 W |
| LOAD+ U1 tab 5 → J3.1 | 3.29 mm | 63 µΩ | **9 µΩ** | 1.42 W | 0.20 W |
| GND J1.2 → J3.2 | 24.58 mm | 315 µΩ | **63 µΩ** | 7.08 W | 1.41 W |
| **Total** | | | | **11.74 W** | **4.01 W** |

At 60 A continuous that is **0.64 W** with bars. The sensor remains the largest single
contributor, which is the right place for it — you cannot design that part away.

**The bus bars remain mandatory, but for a different reason than rev A supposed.**
Putting J1/J3 pin 1 directly on U1's terminal column left the positive segments only
2.3 mm and 3.3 mm of pour, so they are nearly free either way. **The whole problem is now
the ground return**, which has to run the full 24.6 mm past the sensor and carries 7.1 W
bare — 60 % of the total loss. That is the segment the bar has to cover.

Each soldermask opening therefore spans its segment's **entire** current path, pad edge to
pad edge. A bar only conducts where it is bonded; an opening shorter than the run leaves
the uncovered part at full PCB resistance.

### Layout consequences

The connectors need 33 mm of the 95 mm width for XT60 + XT30 side by side. The current
block therefore occupies roughly a 40 × 50 mm region, leaving about **55 × 50 mm for the
MCU, FPC connector, analog front end, buttons and test points** — roughly 56 % component
fill overall, which is comfortable on 4 layers.

Keep ≥ 2 mm between the exposed positive and negative bus channels. IPC-2221 only demands
~0.4 mm at 60 V, but exposed copper carrying molten solder during assembly deserves the
margin.

### Considered and rejected: Waveshare RP2350-Plus

Evaluated 2026-08-11. **Rejected for Rev A.**

The RP2350-Plus offers 26 GPIO against the RP2040-Zero's 20, but it is the RP2350**A**
and still has only **4× 12-bit ADC**. The binding constraint on this design was never
digital pins — it was ADC channels, which is why the second thermistor and the
thermocouple were cut in §4. Six more digital pins buys none of that back.

| Factor | Verdict |
|---|---|
| ADC channels | 4, unchanged — does not solve the actual constraint |
| Digital GPIO | +6, but nothing here needs them |
| Separate SPI for the SD | Not possible: the SD is hard-wired to the display's shared 14-way bus |
| Speed | 150 MHz M33 + FPU vs 133 MHz M0+. Bottleneck is SPI to the display, not the core |
| Board area | Pico form factor ≈ 51 × 21 mm = 1070 mm² vs 423 mm² — **+650 mm²**, a 28 % hit to the non-bus budget |
| Erratum RP2350-E9 | GPIO input-mode leakage up to 120 µA. Into this design's 50 kΩ thermistor source that is meaningless data. Avoidable and fixed in A4 silicon, but a real hazard to introduce for no gain |

**Where it would be worth revisiting:** the RP2350**B** parts carry **8 ADC channels** and
~41 GPIO. That would restore the second thermistor and allow a thermocouple — a genuine
capability increase, and the right trigger for a Rev B. Decide that after using Rev A,
when the missing channels are known rather than guessed.


### Display module, as measured

| | |
|---|---|
| Module PCB | 98.0 × 55.7 mm |
| Mounting holes | 91.5 mm (X) × 49.7 mm (Y) centre-to-centre, Ø3.2 mm |
| Hole insets | 3.0 mm left / 3.5 mm right / 3.0 mm top and bottom — **not centred**, 0.25 mm offset |
| Depth behind screen | 8 mm to the SD slot, **14.5 mm including the pin header** |

The module is **wider than the board and its hole spacing (91.5 mm) exceeds any board
dimension**, so it cannot bolt to the PCB in any orientation. It mounts to the enclosure
instead; H5–H8 were removed. Assume the 14.5 mm depth when designing the enclosure — the
header is staying on.

## 9. Footprints and test points

### Footprint library — complete

`LCM.pretty` holds every non-stock footprint. All were imported from vendor sources and
then **pad-remapped**, because a pad whose name does not match a symbol pin number simply
carries no net — silently, with no error.

| Footprint | Source | Remap applied |
|---|---|---|
| `ACS770_CB_PFF` | Ultra Librarian | 32 stitching vias `4_1…5_16` → `4` / `5` |
| `RP2040_Zero_Castellated` | Ultra Librarian | renumbered to this project's pin order, by signal name |
| `XT60PW-M` / `XT60PW-F` | Ultra Librarian | none needed |
| `XT30PW-M` / `XT30PW-F` | Ultra Librarian | `P`→`1`, `N`→`2` |
| `FPC_05F_14PH20_P0.5mm` | EasyEDA | none needed |
| `SW_TS-1187A_5.1x5.1mm` | EasyEDA | `1,3`→`1`, `2,4`→`2` |
| `TestPoint_D1.5mm`, `RailPad_THT_D1.0mm` | this project | — |
| `MountingHole_M3_3.2mm` | this project | unplated by choice, see below |
| `Potentiometer_Bourns_3362P_Vertical` | this project | stock KiCad name did not resolve; built from the Bourns datasheet |
| `SolderJumper_3_P1.3mm_Open` | this project | stock KiCad name did not resolve; own design |

The ACS770 remap matters most: those 32 perimeter vias are the datasheet's recommended
current-handling stitching. Left as imported they would have been unconnected copper.

**Connector gender is not interchangeable.** J1/J2 are male (the input mates the
battery's female half) and J3/J4 female (the output mates the ESC's male half). M and F
have mirrored pads — swapping them reverses polarity.

Everything else uses stock KiCad footprints; see the assignment table in the schematic.

### Mounting

`H1`–`H4` are M3 board mounting holes; `H5`–`H8` are for display-module standoffs and
their positions are **not yet set** — they need the Hosyond module's hole pattern
measured first. All are zero-pin symbols, so they live in the netlist and survive
"Update PCB from Schematic" with *delete extra footprints* enabled.

The holes are **unplated on purpose**. A plated hole tied to GND, with a metal screw in
it, sitting a couple of millimetres from a 60 V bus, is a short waiting for a bad day.
Unplated costs nothing here since the enclosure is printed plastic.

### J6 removed

The 2.54 mm display header duplicated all 14 signals of the FPC connector and cost 35 mm
of board width plus 14 through-holes through the routing space. With the FPC committed
to, it was deleted — symbol, 14 wire stubs and 14 labels. Net count stayed at 38.


### Two things to check physically

1. ~~**RP2040-Zero pad spacing.**~~ **RESOLVED — verified against Waveshare's own
   mechanical drawing.** Every dimension matches:

   | Dimension | Footprint | Waveshare drawing |
   |---|---|---|
   | Board | 18.00 × 23.50 | 18.00 × 23.50 |
   | Side column inset from edge | 1.38 | **1.38** |
   | Side column spacing | 15.24 | 18.00 − 2 × 1.38 |
   | Side column top / bottom inset | 1.59 / 1.59 | **1.59** |
   | Bottom row outer inset | 3.92 / 3.92 | **3.92** |
   | All pitches | 2.54 | **2.54** |

   The `1.38` on the drawing is edge-to-castellation-centre. Note when checking this
   yourself: the two side columns each have a pad on the bottom row's Y coordinate, so a
   naive "pads at y = bottom" query returns 7 pads inset 1.38 mm and appears to
   contradict the 3.92 figure. The true bottom row is 5 pads; 9 + 9 + 5 = 23.

   A community footprint (CountParadox/RP2040-Zero-Kicad) agrees on the same grid, though
   that repo carries a "DO NOT USE" warning from its author — it was used only as a
   cross-check and its file is not in this project.

2. **ACS770 terminal slots.** The two available footprints disagree: Ultra Librarian says
   0.8 mm signal drills and 9.0 × 4.5 mm terminal slots, EasyEDA says 0.91 mm and
   9.5 × 5.0 mm. UL is used here for its stitching vias. Measure a terminal tab when the
   sensor arrives; the slot can be widened without losing the vias.

### Test points

**10 probe pads plus 4 spare rail pads**, in block K of the schematic. They attach by net
label like everything else, so they joined existing nets without creating any — net count
is 38 before and after.

- **Analog / calibration (TP1–TP10):** `PACK+`, `LOAD+`, `+5VS`, `I_RAW`, `I_SENSE`,
  `V5_SENSE`, `VPACK_TAP`, `V_PACK`, `T_NODE`, `T_SENSE`. `I_RAW` and `I_SENSE` either
  side of the divider let you check the scaling directly; `PACK+`/`LOAD+` let you measure
  the board's own IR drop under load with a four-wire setup. This is exactly the set §6
  walks through, which is why they are kept as one block rather than distributed — you
  probe them in sequence with a DMM.
- **Spare rails:** J9 `+3V3`, J10 `+5V`, J11/J12 `GND`, J13 `GP0` — 1.0 mm plated holes,
  so they take a probe ground spring as well as a soldered wire.

**Every probe pad is silkscreened with its NET NAME, not its designator.** During
calibration you are hunting for `I_SENSE` or `T_NODE`, not for "TP5". The grid pitch is
set by the text, not the pads: 9 mm columns give 4.5 mm of clear silk between labels
(`VPACK_TAP` is the longest at ~4.5 mm), and 6 mm rows leave 2.25 mm above each pad's silk
ring. The `silk` stage reads each name off the footprint's own pad net, so it cannot drift
out of step with the netlist.

### Cut from Rev A: TP11–TP27, C12, C13

The original 27 test points plus rail pads had a **501 mm² bounding box — 8.5 % of the
board, and nearly double the area of every resistor and capacitor on it put together.**
The 13 display-bus and 4 user-IO probe points were removed, taking the block to 223 mm²
and recovering 278 mm² (4.7 % of the board) as one contiguous region above U2.

`C12`/`C13`, the button debounce caps, went with them — firmware debounce is more
flexible and SW1/SW2 are not timing-critical.

**What still has probe access.** `SPI_SCK`, `SPI_MOSI`, `SPI_MISO` and `DISP_LED` are
probeable at R11–R14's pads, and `DISP_VCC` at JP1. That matters: §11 flags the microSD
MISO tri-state problem as the risk to test early, and R13 sits in that exact path, so the
diagnosis route is intact.

**What lost it.** `LCD_CS`, `LCD_RS`, `LCD_RST`, `SD_CS` and the four `CTP_*` lines have
no series resistor and no probe pad. They run to J7, which is 0.5 mm pitch and not
practically probeable. If any of those need scoping during bring-up, tack a wire to the
resistor-side via or fit a temporary pad.

> **Why the passive count looks high and is not.** Of 18 resistors, five (R10–R14) are
> 0 Ω placeholders — options to fit later, not circuitry — and R15/R16 are marked
> not-fitted because the RP2040's internal pull-ups are used. Eleven do electrical work:
> five divider legs, three series-protection, two ESC, one thermistor leg. Of the 11
> capacitors, five are the ADC/sensor charge reservoirs the accuracy story depends on
> (§4.3, §4.4) and six are supply decoupling across three rails. All 29 together occupy
> **4.5 % of the board**; the connectors and the two modules take 36 %.

Signal pads are 1.5 mm bare copper with no paste aperture, so reflow leaves them flat and
probe-friendly. All are excluded from BOM and position files.

## 10. Bill of materials

See **`BOM.md`** for the full table with LCSC part numbers, library tier, live stock and
per-line cost, and **`BOM.csv`** for direct upload to JLCPCB.

Summary: 16 SMT lines (10 Basic + 6 Extended), about **$10.84 of parts per board** — of
which the ACS770 alone is $9.98. Everything else together is under a dollar. Nine parts
are hand-soldered: the RP2040-Zero module, four XT connectors, three headers and the trim
pot.

Regenerate with `cd generator && python3 gen_bom.py`; it reads the schematic directly, so
the BOM cannot drift out of sync with the design.

## 11. Firmware architecture

**Toolchain.** Arduino-Pico core (earlephilhower) or PlatformIO. Both handle the RP2040-Zero.

**Sampling (core 1).** ADC in round-robin over ADC0–ADC3 with DMA into a ring buffer.
Oversample and decimate to ~2 kHz effective per channel. Note the RP2040 ADC has known
DNL artefacts near codes 512 / 1536 / 2560 / 3584 on some silicon; averaging 16–64 samples
both fixes that and buys about two extra effective bits.

**UI (core 0).** Display refresh ~20–30 fps, SD writes, button handling, USB CDC output.

**SPI bus sharing.** LCD, touch and SD all hang off SPI0 — all mode 0, so no conflict, but
they want different clocks: LCD 40–62.5 MHz, SD 12–25 MHz. Wrap every access in
`SPI.beginTransaction()` / `endTransaction()` with per-device `SPISettings`. If you use
TFT_eSPI, enable its bus-sharing support rather than letting it own the peripheral.

**Logging.** `SdFat` with a pre-allocated contiguous file and 512-byte buffered writes.
100 Hz CSV is comfortable. Same lines stream over USB CDC so the laptop can capture live.

```csv
millis,v_pack,i_amps,power_w,temp_c,i_peak,p_peak
1042,24.132,87.44,2110.3,41.2,91.06,2198.7
```

**Display.** Rolling current-vs-time graph plus large numerics for V, A, W, and peak-hold
A and W. TFT_eSPI with sprites is lighter and simpler at 480×320; use LVGL only if you
want a real touch UI.

**Buttons.** SW1 cycles display views. SW2 performs the zero/tare of §6.

### Known risk to test early
The microSD on these display modules shares MISO with the LCD. Some cheap modules do not
tri-state the SD's MISO cleanly when `SD_CS` is high, which corrupts LCD reads. **Verify
this before trusting logged data.** If it misbehaves, R13 is already in the MISO path as a
0 Ω placeholder and can become a series resistor or a buffer.

---

## 12. Files and KiCad version

| File | Purpose |
|---|---|
| `Logging_Current_Meter.kicad_sch` | The schematic |
| `LCM.kicad_sym` | All project symbols — fully self-contained |
| `sym-lib-table` | Resolves the `LCM:` nickname |
| `Logging_Current_Meter.kicad_pro` | Project file |
| `sheet_preview.png` | Rendered preview |
| `generator/` | The Python that produced all of it, plus the verifier |

**Format is now KiCad 10.** The files on disk are:

| File | Format version | Last written by |
|---|---|---|
| `Logging_Current_Meter.kicad_sch` | `20260306` | eeschema 10.0 |
| `Logging_Current_Meter.kicad_pcb` | `20260206` | pcbnew 10.0 |
| `LCM.kicad_sym` | `20231120` | the generator |

KiCad refuses to open files newer than itself, so **KiCad 10 or newer is now required**;
an older install will not open the project at all.

The original generator output was KiCad 8 (`20231120`), chosen because it sits below
KiCad 9's ceiling and is read natively by 8, 9 and 10, which made the generated files
portable to whatever KiCad happened to be installed. That mattered while the schematic was
machine-generated. KiCad 10 has since opened, accepted and rewritten both files, and that
bump is one-way.

`generator/kicad8.py` still hardcodes `SCH_VERSION = 20231120` and
`GENERATOR_VERSION = "8.0"`. **Re-running `gen_sch.py` would therefore downgrade the
schematic and discard every edit made in KiCad since.** Treat it as the script that
produced Rev A, not as a tool to run again. `gen_pcb.py` was updated for KiCad 10 and
emits placed KiCad 10 footprint blocks; `gen_sch.py` was not.

Every symbol lives in `LCM.kicad_sym` on purpose. If the schematic referenced stock
libraries, KiCad could re-link a symbol to a stock version whose pin geometry differs from
what the schematic was generated against, silently pulling pins off their wire stubs.
Self-contained removes that failure mode. Swap to `Device:R` and friends later if you
prefer — the netlist does not care.

### Verifying it yourself

```bash
cd generator
python3 gen_sch.py ..        # regenerate everything
python3 verify.py ../Logging_Current_Meter.kicad_sch ../LCM.kicad_sym
```

`verify.py` re-reads the finished files from disk and rebuilds the netlist
**geometrically** — pin connection points, wire endpoints and label anchors that share a
coordinate are one node — then compares it against an independently written expectation
of all 38 nets and 155 pin connections. It also re-checks the ACS770 terminal list, the
display pin order and the RP2040 pin map by *name*, so a transposition cannot hide behind
a pin number that happens to line up. It currently passes clean.

What that does **not** prove: that KiCad's own ERC is happy, or that the file is
byte-perfect KiCad 8 syntax. Those need KiCad itself. Open it, run ERC, and tell me what
it says.

---

## 13. Schematic conventions

Connectivity is by **net label on a short stub at every pin**, not by long point-to-point
wires. This is a normal "signal name" style, it keeps the sheet readable at A2, and it
makes the netlist a pure function of the label table rather than of routing geometry —
which is what allows the machine verification above.

Blocks A–J on the sheet are annotated with the reasoning behind each design choice, so the
schematic stands on its own without this document open beside it.

---

## 14. Layout plan

### Board and stackup

| | |
|---|---|
| Outline | 95 × 50 mm, corners R2 |
| Layers | 4: F.Cu / In1.Cu / In2.Cu / B.Cu |
| Copper | 1 oz outer, 0.5 oz inner (JLCPCB standard 1.6 mm) |
| F.Cu | Positive bus (exposed), components, signals |
| In1.Cu | **GND plane** — negative return and the reference for every signal above it |
| In2.Cu | Signals and +3V3 / +5V distribution |
| B.Cu | Negative bus (exposed) + GND pour |

Both buses run vertically (top edge to bottom edge) as F.Cu + B.Cu pours stitched with a
dense via field, soldermask removed on both sides, sized for a 10 AWG bar along each.

### Regions

As built. Coordinates are board-local millimetres, origin at the top-left corner; the
authoritative table is `PLACEMENT` in `generator/gen_pcb.py`.

**Generated** by `generator/region_map.py`, read straight off the `.kicad_pcb` — do not
edit by hand. The hand-maintained version of this map drifted twice: it was still showing
`TP27`, `C12`, `C13`, `D2`, `R18` and the old column grid long after every one of those had
gone. A label is allowed at most one row of give from the part's true position; anything
that will not fit is reported rather than moved, so the map cannot quietly misplace a part.

```
  u=0                                                                u=95
   +----------------------------------------------------------------------------+ v=0
   |                                                                            |
   |                                                                            |
   |  H1                     J2        TP1    TP2    TP3    TP4    TP5     H2   |
   |                                                                            |
   |                                   TP6    TP7    TP8    TP9   TP10          |
   |            J1                                                              |
   |                                                                            |
   |                                   J9     J10    J11    J12    J13          |
   |                               D1                                           |
   |                                          C4 C5  C7 C6                      |
   |                                          R2 R7  R9 R8                      |
   |                                                                            |
   |                                   C3 R1            R5                      |
   |                                U1    C1                       U2           |
   |                                   FB1    C2                                |
   |                                                                            |
   |                                                                            |
   |                                     C10          RV1                       |
   |                           R3 R4             R10       C8 C9 C11 R14 R17    |
   |            J3                                            R12        J8     |
   |                                  R6              J5   R11   R13            |
   |                                                                            |
   |                         J4         R15     R16      JP1                    |
   |                                                                            |
   |  H3                                  SW1     SW2          J7          H4   |
   |                                                                            |
   |                                                                            |
   +----------------------------------------------------------------------------+ v=62
        CURRENT BLOCK  u <= 42.5        ELECTRONICS  u >= 43
```

> **Known DRC warning: D1 / U1 courtyards overlap by 3.49 × 0.45 mm.** D1 is hand-placed
> at (40.94, 20.00) rot 180. The overlap is courtyard *margin* only — checked, and there is
> no U1 pad, no fab body and no silk inside D1's footprint area; U1's package body starts
> at v = 24 and its nearest terminal pad at v = 21.5. KiCad will still flag it. Nudging D1
> up 0.5 mm clears it if you want a silent DRC.

- **U1 rotated 90°** puts its two terminals (natively side by side, 10 mm apart) into a
  vertical stack, so the current direction is the board's short axis. The 24 mm body then
  projects **right**, landing VIOUT on pin 3 at u = 41.5, directly beside column A of the
  analog front end.
- **J1 and J3 sit at u = 16.5**, which places pin 1 at **u = 20.10 — exactly U1's terminal
  column**. The positive path is a straight vertical drop with no lateral jog. Their GND
  pins land at u = 12.90, giving a second straight run down the left edge.
- **The XT30 taps are not on the 150 A bus.** They are a 30 A convenience and reach GND
  through the In1 plane like any other component, which frees them from the bus channels
  and lets them clear the XT60 bodies.
- **SW1/SW2 are on the bottom edge**, not buried in the analog block — SW2 is pressed by
  hand during every tare.
- **RV1** is 7.5 × 7.15 mm, far too big for the 3 mm analog column pitch, so it sits below
  the block where a screwdriver still reaches it.
- **H1/H3 sit inside the GND channel**, which is harmless: a screw there is at the
  measurement reference. H2/H4 are in the electronics area, well clear of the 60 V bus.

- **U1 rotated 90°** so its terminals align with the current direction, body projecting
  right so the signal pins land nearest the analog section — a short `VIOUT` run.
- **U2 USB-C must reach the right edge.**
- **J7 FPC** near an edge so the ribbon exits cleanly.
- **RV1** reachable with a screwdriver once assembled.
- **H1–H4** at the corners, inset clear of the bus pours.
### The analog front end is lanes, not a grid

The four ADC inputs are **consecutive pads on U2's top row**, all at v = 22.38:

| U2 pad | Net | u |
|---|---|---|
| 7 | `I_SENSE` | 76.42 |
| 6 | `V_PACK` | 78.96 |
| 5 | `T_SENSE` | 81.50 |
| 4 | `V5_SENSE` | 84.04 |

Rev A filled a 3-column grid function-by-function. It read tidily on the schematic but
meant all four of those nets had to leave a column *vertically* and then cross the whole
board *horizontally*, through each other and through C10 and RV1. That is what made the
board hard to route.

So the block is transposed. Each channel gets a horizontal lane, and **the lanes are
ordered top-to-bottom to match U2's pad order left-to-right**, which makes the four runs
parallel and non-crossing by construction:

```
  row 1  v = 20.5   ADC pin caps       C4    C5    C7    C6     <- nearest free lane to U2
  row 2  v = 24.5   last series part   R2    R7    R9    R8
  row 3  v = 28.5   upstream          C3 R1 D1          R5
  row 4  v = 32.5   sensor supply     FB1 C1 C2
                    u = 54.0  58.5  63.0  67.5
```

- **Row 1** holds the charge reservoirs that make the high-impedance dividers work at all
  (§4.3, §4.4), so they take the lane closest to U2. The four runs are 19–25 mm, straight,
  parallel, and `routability.py` confirms **zero crossings** between them.
- **Row 2** holds each chain's last element. R2/R8 are divider lower legs; R7 is the fault
  limiter that caps the clamp current if 60 V ever reaches `VPACK_TAP` through a low
  impedance (§4.3), and R9 the thermistor series — protection belongs at the MCU end, not
  at the source.
- **C3** is the 10 nF datasheet maximum on VIOUT, 3.5 mm from U1 pin 3 — as close as U1's
  courtyard allows.
- **The pack-voltage divider stays down on the LOAD+ pour it taps** (R3/R4 at v = 42.5),
  so the only 60 V net in the electronics half is one short hop from the pour into R3.
  `VPACK_TAP` then runs up to R7/D1/C5. That costs 31 mm of MST on a 9.5 kΩ node — which
  is free electrically and belongs on In2.Cu — and in exchange keeps the high voltage out
  of the signal lanes entirely.

Measured by `generator/routability.py` (Manhattan MST per net, poured nets excluded):
**968 mm total against 1058 mm, −8.5 %.** The headline number understates it; the four
analog channels are where it landed:

| Net | Before | After |
|---|---|---|
| `T_NODE` | 91.9 mm | **53.4 mm** |
| `V5_SENSE` | 91.6 mm | **61.3 mm** |
| `V_PACK` | 66.1 mm | **52.0 mm** |
| `T_SENSE` | 52.0 mm | **38.5 mm** |
| `T_POT_TOP` | 19.5 mm | **6.0 mm** |
| `I_SENSE` | 66.0 mm | **58.9 mm** |
| `VPACK_TAP` | 28.4 mm | 59.9 mm — deliberate, see above |

### Passive designators are on Dwgs.User

27 resistor and capacitor designators on a 95 × 62 board is more silkscreen than it is
worth — they crowd the pads and collide at this density. All 23 non-placeholder R/C
references moved to **`Dwgs.User`** (KiCad's "User.Drawings"), where they still print on
documentation and stay visible while editing but leave the physical silk clean.

`Dwgs.User` rather than `F.Fab`, because these footprints already carry an
`fp_text user "${REFERENCE}"` on F.Fab — moving the property there would print the
designator twice on the fab drawing.

> **The five 0 Ω placeholders keep their silkscreen: R10–R14.** These are the parts most
> likely to be changed by hand on an assembled board — R10 becomes 91k for a stable
> thermistor trim, R11–R14 become 22R if the display bus rings, and R13 in particular is
> the one you will be reaching for if the microSD MISO problem in §11 is real. You cannot
> rework a part you cannot find. Detected by `Value == "0R"` read off the board, not from
> a list in the generator.

Everything that is not a resistor or capacitor keeps its silk: D1, FB1, RV1, the
connectors, switches, mounting holes, test points, U1 and U2.

### Net classes

| Class | Nets | Clearance | Notes |
|---|---|---|---|
| `HV_BUS` | `PACK+`, `LOAD+` | **0.5 mm** | Exposed pour, 60 V |
| `GND_BUS` | `GND` | **0.5 mm** | Exposed pour + In1 plane |
| `PWR` | `+3V3`, `+5V`, `+5VS` | 0.2 mm | 0.5 mm tracks |
| `Default` | everything else | 0.2 mm | 0.25 mm tracks |

Vias 0.6/0.3 mm (0.8/0.4 on the bus classes); edge clearance 0.5 mm.

> **0.5 mm, not the 2.0 mm rev A specified.** The ACS770's two terminals are **1.00 mm
> apart** — u [17.85, 22.35], v [21.50, 30.50] and v [31.50, 40.50] — which is the moulded
> part, not something layout can change. A 2 mm copper clearance is therefore an
> unsatisfiable DRC error on every board built.
>
> The 2 mm was never an electrical requirement: rev A's own note says *"IPC-2221 only
> demands ~0.4 mm at 60 V — the 2 mm is for the assembly reality of flooding exposed
> copper with solder"*. That reasoning is right, but it applies to the **soldermask
> apertures**, which is where molten solder can actually flow — not to the copper. So the
> copper takes 0.5 mm (comfortably above IPC) and the apertures are laid out ≥ 2 mm apart,
> which `verify_pcb.py` check 6 enforces.

### Pours, as built

| Zone | Net | Layers | Extent (board-local) |
|---|---|---|---|
| GND plane | `GND` | In1.Cu | whole board, inset 0.8 mm |
| GND bus | `GND` | F.Cu + B.Cu | u [0.5, 15.5], full height |
| PACK+ bus | `PACK+` | F.Cu + In2.Cu + B.Cu | u [17.6, 33.0], v [0.5, 30.7] |
| LOAD+ bus | `LOAD+` | F.Cu + In2.Cu + B.Cu | u [17.6, 33.0], v [31.3, 61.5] |

The two channel outlines are 2.1 mm apart. PACK+ and LOAD+ stop 0.2 mm clear of their
respective ACS770 terminal groups, leaving 0.6 mm between them — tight, but it is the
part's own geometry and the 0.5 mm clearance permits it.

**180 stitching vias** (0.8/0.4 mm) on a 2.5 mm grid: 96 on GND, 43 on PACK+, 41 on
LOAD+. Each is placed only where it clears every foreign pad by 1.6 mm; `verify_pcb.py`
check 6 independently confirms none lands on a pad of another net, which would be a dead
short no amount of zone clearance would save you from.

### Why the board file is built by KiCad first

The stock footprints (0805, SOT-23, headers, pot, electrolytic, solder jumper) live in the
KiCad installation, not in this project, so the board cannot be generated from scratch
here. The sequence is:

1. Open the PCB editor and run **Tools → Update PCB from Schematic (F8)**. KiCad creates
   the board in its own native format and pulls in every footprint. *This also serves as
   the outstanding check that all stock footprint names resolve.* **Done** — all 82
   footprint names resolved.
2. Save. The file lands in this folder.
3. Placement, outline, zones, keepouts and rules are then applied by editing that file —
   the same approach used on the schematic, so nothing hand-placed ever gets overwritten.

### `generator/gen_pcb.py` — what it has done

The board file is **KiCad 10 native (`20260206`)**. The schematic has since caught up and
is now `20260306`, also KiCad 10. Both were written by KiCad itself, and there is nothing
to gain from forcing either back to the generator's `20231120`.

| Stage | Effect |
|---|---|
| `sync` | Added `RV1` and `JP1`, which were put in the schematic after the last F8, from `LCM.pretty` with their nets and their schematic symbol uuid. 80 → **82** footprints |
| `prune` | Removes footprints no longer in the schematic, **and** any whose library ID the schematic has reassigned — those are dropped so `sync` rebuilds them, which is what F8 does. J8 went 1x04 → 1x03 this way |
| `renet` | Renames pad nets the schematic has renamed. `prune`/`sync` reconcile which *footprints* are on the board and touch nothing on the ones that survive, so U2 pin 10 sat on the dead `/ESC_TELEM_MCU` until this stage existed |
| `silk` | Labels each probe pad with its net name and hides its designator |
| `refdes` | Moves R/C designators to `Dwgs.User`, keeping the five 0 Ω placeholders on silk |
| `pour` | In1 GND plane, three bus zones, six soldermask openings, 180 stitching vias |
| `stackup` | 2 → **4 copper layers** (F.Cu / In1.Cu / In2.Cu / B.Cu). Netclasses `HV_BUS`, `GND_BUS` (2.0 mm clearance) and `PWR` (0.5 mm track) with pattern assignments, plus the DRC minimums, into `.kicad_pro` |
| `outline` | Edge.Cuts **95 × 62 mm, R2 corners**, board origin at page (50, 50) |
| `place` | All **63** footprints to the `PLACEMENT` table |

Every stage is idempotent and takes a timestamped backup. Re-run the lot with
`cd generator && python3 gen_pcb.py ..`.

> **No explicit `(stackup)` block was written.** KiCad synthesises a default 4-layer
> 1.6 mm stackup on open. Set the 1 oz outer / 0.5 oz inner copper weights in
> **Board Setup → Physical Stackup** — they are a fab note, not something DRC consumes.

### Two format assumptions, now confirmed

These were the only things in the board file not confirmed against a KiCad-written
example, and both were load-bearing. **Both are now verified.** KiCad 10 opened the board,
accepted it without complaint, and rewrote it as format `20260206` with
`(generator "pcbnew") (generator_version "10.0")`. A KiCad-written file is exactly the
evidence that was missing.

1. **Inner copper layer ordinals.** Assumed KiCad 9 renumbered the layer enum so copper
   takes the even ordinals: F.Cu = 0, B.Cu = 2, and every even ordinal above 2 unused, so
   In1.Cu = 4 and In2.Cu = 6. **Confirmed.** The saved file contains `F.Cu`, `In1.Cu`,
   `In2.Cu` and `B.Cu`, and Board Setup reads four copper layers.
2. **Zone and via net syntax.** Written as `(net "GND")` by name rather than the KiCad 6–8
   `(net 1) (net_name …)` pair. **Confirmed.** The saved file still has no top-level
   `(nets …)` section for an index to resolve against, and KiCad preserved the name form
   on its own write. The published format docs at dev-docs.kicad.org predate this change
   and could not settle it; the file itself now does.

Had either been wrong, KiCad would have refused the file and named the offending line, the
same way it caught the quoted-tag bug: noisy, not silent, and a one-line fix.

### Verifying the board

```bash
cd generator
python3 verify_pcb.py ../Logging_Current_Meter.kicad_pcb ../Logging_Current_Meter.kicad_sch
python3 render_pcb.py ../Logging_Current_Meter.kicad_pcb /tmp/pcb.svg
```

`verify_pcb.py` is to the board what `verify.py` is to the schematic: it re-reads the
finished file off disk and re-derives every position from it, so it can disagree with the
generator. It checks **quoted tags** (below), the footprint inventory against the
schematic, that every electrical pad carries a net, that every pad clears the board edge,
and that **no two courtyards overlap**. Footprints with no `F.CrtYd` at all —
`SW_TS-1187A`, the FPC connector — fall back to their pad extent rather than being
silently exempted from the overlap check.

> **The quoted-tag trap.** Every s-expression tag must be a *bare* token. The first build
> of `gen_pcb.py` emitted `("uuid" "…")` instead of `(uuid "…")`, because the helper that
> serialises the tree quotes any plain Python string and only leaves a `Sym` bare. The
> file parses perfectly with any reasonable reader, the diff looks right, and KiCad
> refuses to open the board at all:
>
> ```
> Expecting layer, hide, effects, locked, render_cache or tstamp.
> Got 'quoted string' … line 14731, offset 5.
> ```
>
> Every tag `gen_pcb.py` emits now goes through the `S()` helper, and `verify_pcb.py`
> greps for `("…"` as check 0 — it is the cheapest check and it fails the whole file, so
> it runs before anything else.

Check 5 fails on any board net the schematic no longer declares — the check that caught
`/ESC_TELEM_MCU` still sitting on U2 pin 10 after the rename. Check 7 fails on any
component sitting inside a soldermask opening, which caught R3 placed in the middle of the
LOAD+ exposed strip, where solder flooding would have swamped it. The bus terminals
themselves (J1–J4, U1) are exempt by name, since the apertures exist to expose the copper
around them — listed explicitly rather than inferred from "has a pad on this net", because
R3 taps LOAD+ too and still has to be caught.

`routability.py` estimates copper cost so a claim that one placement routes better than
another can be checked: per net it builds a Manhattan minimum spanning tree over the pad
centres and sums the edges. Poured nets (GND, the two bus nets, and the In2 power rails)
are excluded — their MST says nothing useful.

Current state: **clean.** 62 footprints, 37 nets, 0 courtyard overlaps. The 10 pads
reported as netless are connector shroud legs and FPC hold-down tabs, which correctly
carry no net; the check distinguishes them by the absence of a `pinfunction`.

> **The empty-property trap.** Both this file and `gen_pcb.py` read "which schematic
> symbols have a footprint" by walking paren-balanced symbol blocks, never by a regex
> pairing `"Reference"` with the next `"Footprint"`. That regex runs straight past any
> symbol whose footprint is empty — every power flag on this sheet — and pairs its
> reference with the *following* symbol's footprint. The first `prune` did exactly that
> and deleted six live parts (C1, J11, R4, R10, R11, TP9) before the count check caught
> it.

### Schematic surgery

`strip_sch.py` removes symbols together with the wire stubs and net labels that belong to
them. It does not hardcode stub geometry: it reads each symbol's pin offsets out of
`lib_symbols`, removes only wires that actually end on a pin point, then removes labels
sitting at the far end of those wires. Leave a stub behind and you have an orphan label
still declaring a net; take one too many and a real connection vanishes silently.
`verify.py` is the check on all of it, and passes.

It also discovers the block K grid origin from TP1's live position rather than assuming
`add_testpoints.py`'s original coordinates — the whole block has since been dragged in
KiCad by (−198.12, −53.34), and hardcoding would have flung the rail pads back across the
sheet, undoing hand placement that §12 exists to protect.

