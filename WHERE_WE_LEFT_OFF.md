# Where we left off — 2026-09-05

Handoff note for picking this up in a fresh session. `DESIGN.md` is the real documentation;
this file is just the state of play and what to do next.

---

## One-line status

**Rev A is built, assembled, calibrated, and running a real touchscreen UI.** The dead-button
problem is solved: `firmware/logging_current_meter_ui/logging_current_meter_ui.ino` (currently
**v1.7**) is a from-scratch touch-driven UI — Home, Live View, Graph, Calibrate, and a new
Dev Mode diagnostic screen — built as a sibling sketch next to the `display_bringup`
diagnostic (which is untouched). It has been through several rounds of real bench-test
feedback and fixes. **Open issue:** small touch targets are still inconsistently hittable —
see below.

---

## Next task

### Touch reliability on small targets — still not fully solved

Seth's v1.7 bench-test feedback (2026-09-05): the two overlap/hold-drop bugs reported after
v1.6 are fixed, but **small touch targets (Back buttons, V/T show chips) are still difficult
to hit reliably**, even after several rounds of fixes (interrupt-based touch, `TOUCH_HIT_SLOP`
hit-region expansion, and a hold-debounce fix). This is the open item to pick up next.

What's already been tried, in order, and is already in the code — don't re-do these:
1. Interrupt-driven touch polling (`CTP_INT` ISR + `TOUCH_POLL_MS=20` fallback timer),
   replacing pure polling — done in v1.3.
2. `TOUCH_HIT_SLOP=4` px hit-region expansion around every button, checked against every
   button-to-button gap in the layout — done in v1.4.
3. A `SCR_DEV` Dev Mode diagnostic screen (new Home tile) added specifically so Seth could
   see raw/mapped touch coordinates, a live crosshair, and per-target hit reporting on
   hardware — done in v1.6, overlap bug in its own layout fixed in v1.7.
4. Touch-hold debounce (`TOUCH_MISS_TOLERANCE=3` consecutive misses before treating a held
   touch as released) — done in v1.7, fixed a different symptom (holding down would
   intermittently drop) but did **not** fix general small-target hit reliability.

Likely next things to try, not yet attempted:
- Use the Dev Mode screen's raw-vs-mapped coordinate readout to check whether taps near a
  target's edge are landing where expected at all, or whether there's a mapping/offset
  error specific to certain screen regions (not just a "targets are too small" problem).
- Try the FT6336U in **polling mode** (`0xA4=0x00`) instead of the current **trigger mode**
  (`0xA4=0x01`) — considered during the v1.7 round but deliberately deferred in favor of the
  simpler debounce fix first. Trigger mode only pulses `CTP_INT` on state *changes*, which
  could plausibly be part of why small/brief taps are missed.
- Consider whether the touch controller's report rate or the `TOUCH_POLL_MS=20` fallback
  cadence itself is too coarse for a fast tap-and-release on a small target — a tap shorter
  than one poll interval could land and lift between samples.
- Re-check `TOUCH_HIT_SLOP` isn't being clipped by a still-too-tight gap somewhere once the
  actual failure mode is better understood from Dev Mode data.

Ask Seth for specifics before guessing further: which exact buttons fail most often, whether
it's a "doesn't register at all" or "registers the wrong target" problem, and whether Dev
Mode's live coordinate readout (Home → Dev Mode) shows anything obviously wrong when a normally-missed
tap is attempted.

---

## Touchscreen UI firmware — current state

`firmware/logging_current_meter_ui/logging_current_meter_ui.ino`, v1.7 as of 2026-09-05.
Full version history is in the file's own header comment block — read that for the
blow-by-blow of every fix (numbers vanishing after reset, flicker, graph axis truncation,
layout reworks, the Dev Mode overlap bug, the touch-hold debounce). Screens: Home (5 tiles:
Live View / Graph / Log-disabled-stub / Calibrate / Dev Mode), Live View, Graph, Calibrate
(read-only, serial-only entry), Dev Mode (touch diagnostic: crosshair, raw/mapped coords,
per-target hit reporting, loop-rate counter).

Delivered to both:
- `C:\Users\Seth Schaffer\OneDrive\Projects\KiCAD\Logging_Current_Meter\firmware\logging_current_meter_ui\logging_current_meter_ui.ino`
- `C:\Users\Seth Schaffer\Documents\Github-LOCAL-PROART\Just_Cuz_Logging_Current_Meter\firmware\logging_current_meter_ui\logging_current_meter_ui.ino`

**Not yet committed/pushed to GitHub as of this note** — that's still Seth's to run from
`Github-LOCAL-PROART` (a fresh Claude session in this repo cannot run git commands directly;
no shell access to Seth's machine is available from that environment). Suggested commit
message once he's ready:

```
git add firmware/logging_current_meter_ui/logging_current_meter_ui.ino
git commit -m "logging_current_meter_ui: v1.7 - fix Dev Mode overlap, touch-hold debounce"
git push
```

---

## Current gain calibration

Still the only uncalibrated channel as of the last hardware note — check `c` in
`firmware/display_bringup/` (or the new UI's Calibrate screen) to confirm whether this has
since been done; if `I_SENS_CAL` still shows NOMINAL rather than FITTED, it hasn't.

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
   Rev A bodge is to lift two legs per switch keeping 1 and 4. (This is the reason the UI had
   to become fully touch-driven.)

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
  description of where a touch was. (This is now built into the UI itself as the Dev Mode
  screen, not just a bring-up-time check.)
- **A fit returning a physically implausible part means the data is wrong, not the part.**
  The sketch now rejects B outside ±15 % and R25 outside ±25 % of nominal.
- **A full-frame `fillScreen()` costs ~150–200 ms at 12 MHz SPI and blocks the whole `loop()`**
  (including touch polling) for that duration — every screen in the UI paints static chrome
  once on entry and only redraws the specific fields that changed per tick. Don't reintroduce
  a per-tick full repaint anywhere, including future screens.
- Any function taking a `Rect` or `ScreenId` parameter needs a manual forward declaration in
  the UI sketch, or the Arduino IDE's auto-generated prototype block (inserted before those
  types are declared) fails to compile.

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

`DESIGN.md` was substantially updated on 2026-08-25 (before the touchscreen UI build):

- **§4.1.2** new — sensor supply and decoupling, and why R5 taps `+5VS`
- **§4.4** — corrected the 91k R10 recommendation (it lands at 9 % of pot rotation, not a
  fine trim; 47k centres it)
- **§5** — measured accuracy figures against the predictions
- **§6** — rewritten from hardware. Voltage is two-point *with offset*, not one point
  through the origin. No ice bath.
- **§9** new — the three assembly gotchas above
- **§11** — sampling architecture, channel bandwidth table, display refresh ceiling
- **§14** — display and touch constants

It has **not** been updated with the touchscreen UI's architecture (screens, cross-core
data ownership, fixed-point conventions, formatter functions) — that detail currently only
lives in the UI sketch's own header/plan history and in this repo's prior Claude session
transcripts, not in `DESIGN.md` itself. Worth folding in if `DESIGN.md` is revisited.

`BOM.md` is still auto-generated and untouched. `LCM_Purchased_BOM.xlsx` is local-only and
gitignored — it contains order numbers and affiliate links.
