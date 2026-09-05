# Where we left off — 2026-08-25

Handoff note for picking this up in a fresh session. `DESIGN.md` is the real documentation;
this file is just the state of play and what to do next.

---

## One-line status

**Rev A is built, assembled and brought up.** Display, capacitive touch, backlight, all four
analog channels and the ESC output all work. Voltage and thermistor are calibrated. The
buttons are dead from a footprint fault, and the current gain is the last uncalibrated
channel.

---

## Next two tasks, in order

### 1. Current gain calibration

The only uncalibrated channel. Everything else is done and baked in.

Run `i` in `firmware/display_bringup/`. It does tare first (already measured, but re-tare
anyway — the ACS770 keeps up to 400 mA of magnetic offset after a large pull), then asks
for a known current.

**Fit the gain at the highest current you can source.** §5's nonlinearity is ±1.5 A of full
scale, *fixed at any reading*, so it becomes the gain uncertainty in proportion to the
calibration current:

| Cal current | Gain uncertainty |
|---|---|
| 4 A | ±37 % |
| 10 A | ±15 % |
| **30 A** | **±5 %** |
| 150 A | ±1 % |

Use 4 A as the step-3 **check** point, never for the fit.

The wire load (~90 mΩ from an XT60) needs 2.70 V for 30 A and dissipates 81 W. Board
dissipation at 30 A on a bare PCB with no bus bars is only 0.47 W, so the copper bars are
not needed for this.

Then paste `I_QUIESCENT_CAL`, `I_SENS_CAL` and `I_CAL_CURRENT` into the top of the sketch.

### 2. Touchscreen UI

**The buttons are dead, so every function must be reachable by touch.** Nothing is designed
yet. What exists today is a serial command menu; the equivalent needs to exist on the panel.

Functions that currently have no touch route: tare, view switching, peak reset, start/stop
logging, calibration entry.

Relevant measured constraints, all in `DESIGN.md` §11:

- Full-frame redraw is 4.9 fps at 12 MHz and 25 fps even at 62.5 MHz — never repaint the
  whole screen.
- Partial updates are limited by per-call overhead, not bandwidth: ~1550 calls/frame gives
  43 fps, one window-write per column gives 172 fps.
- The ST7796 has a hardware vertical scroll (`0x33`/`0x37`) and at rotation 1 the panel's
  native vertical axis *is* the graph's time axis — that makes a scrolling plot one
  register write per frame. Arduino_GFX does not expose it; it would go through the databus
  directly. This is the route to a 60 fps standalone camera mode.
- Core 1 samples, core 0 draws. They contend only on the ADC, which core 0 must not touch
  while sampling runs.

---

## Calibration constants as they stand

Baked into `firmware/display_bringup/display_bringup.ino` (v2.1) and applied from boot.
Press `c` to print them.

| Constant | Value | Status |
|---|---|---|
| `V_GAIN_CAL` | `0.01662778f` | fitted, 9.99 V + 57.03 V |
| `V_OFFSET_CAL` | `-0.13632f` | fitted |
| `I_QUIESCENT_CAL` | `0.105399f` | measured at 0 A |
| `I_SENS_CAL` | `0.00533200f` | **NOMINAL — not fitted** |
| `RV1_OHMS` | `100830.0f` | measured, FNIRSI |
| `NTC_B` | `3836.6f` | fitted, 21.1 + 100 °C |
| `NTC_R25` | `97988.0f` | anchored on 111.190 k at 22.1 °C, dry |
| `LCD_ROTATION` | `1` | |
| `TOUCH_INVERT_X` / `_Y` | `0` / `0` | |

**One honest gap:** both voltage fit points read zero error *by construction*. A third
voltage, ~30 V, is the only real validation. §5 predicts under 0.09 V of error.

---

## Rev A hardware faults — three, all documented in `DESIGN.md` §9

1. **`JP1` ships open.** It is the display's only supply. Bridge pad 2 (`DISP_VCC`) to pad 1
   (`+5V`). Until you do there is no backlight, no SPI and no I²C, which looks like a far
   bigger fault than it is.
2. **`J7` needs a TYPE A (same-side) FFC cable.** The one supplied with the display is
   Type B and reverses the pin order — 5 V lands on the module's `SD_CS` while `VCC` sits
   unpowered. The netlist and footprint are correct; this is purely a cable-type
   requirement.
3. **SW1/SW2 pad mapping is wrong.** The `SW_TS-1187A` remap assumed internal pairs 1–3 and
   2–4; they are 1–2 and 3–4, so both buttons short `BTN1` to `GND` permanently. Rev B fix;
   Rev A bodge is to lift two legs per switch keeping 1 and 4.

Plus the two pre-existing items: the **RV1 footprint** is fixed in the library but not
applied to the board, and the **D1/U1 courtyard overlap** is benign and should not be
re-investigated.

---

## Things that will bite if forgotten

- **Never use an ice bath for the thermistor.** Wet leads shunt it by ~340 kΩ — a 49 %
  error at 0 °C and 2 % at 100 °C. That temperature-dependent shape cannot be absorbed by a
  two-parameter fit, which is why three attempts produced three different impossible
  answers. Room temperature plus boiling gives 77 °C of spread and settles B to 0.18 %.
- **Backlight must be driven solid HIGH, never PWM'd.** `I_SENSE` passes to 10 kHz but
  `V5_SENSE` only to 318 Hz, so the §4.2 ratiometric correction is only valid below
  ~318 Hz. `analogWrite`'s default ~1 kHz lands in that gap and injects current noise no
  calibration can remove.
- **The ADC is one shared peripheral.** Core 0 must not touch it while core 1 is sampling.
- **`gfx->begin()` returning true proves nothing** — Arduino_GFX does not read back from the
  panel. It returns true with the ribbon unplugged.
- **The crosshair under the finger is the ground truth for touch mapping**, not a verbal
  description of where a touch was.
- **A fit returning a physically implausible part means the data is wrong, not the part.**
  The sketch now rejects B outside ±15 % and R25 outside ±25 % of nominal.

---

## Serial commands in `display_bringup`

115200 baud. **Set the line ending to Newline** or nothing registers.

| | |
|---|---|
| `a` / `A` | analog report / streaming |
| `b` | button report |
| `c` | show stored calibration |
| `s` | GPIO bridge/short test (ribbon out first) |
| `k` | backlight ramp |
| `m` | MISO read-back (§11) |
| `d` | display test pattern |
| `t` / `x` | touch scan / coordinate stream |
| `g` | **live V/I graph, 5 s scrolling window** |
| `e` / `l` | ESC output / GP0 loopback |
| `n` / `v` / `i` | thermistor / voltage / current calibration |
| `r` / `?` | full re-run / help |

---

## Documentation state

`DESIGN.md` was substantially updated on 2026-08-25:

- **§4.1.2** new — sensor supply and decoupling, and why R5 taps `+5VS`
- **§4.4** — corrected the 91k R10 recommendation (it lands at 9 % of pot rotation, not a
  fine trim; 47k centres it)
- **§5** — measured accuracy figures against the predictions
- **§6** — rewritten from hardware. Voltage is two-point *with offset*, not one point
  through the origin. No ice bath.
- **§9** new — the three assembly gotchas above
- **§11** — sampling architecture, channel bandwidth table, display refresh ceiling
- **§14** — display and touch constants

`BOM.md` is still auto-generated and untouched. `LCM_Purchased_BOM.xlsx` is local-only and
gitignored — it contains order numbers and affiliate links.
