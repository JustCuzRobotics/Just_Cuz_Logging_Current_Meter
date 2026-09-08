# Detailed Commit Timeline

The complete account of every commit, newest first. Commit messages stay short
and scannable; this file holds the full detail — what changed in each file, why,
how it was verified, and what remains open. Entries are grouped by version where
the project has one, otherwise by date, so a section can be lifted into release
notes with light editing.

<!-- Newest entries go directly below this line. -->

## v3.1c — 2026-09-08

### firmware: v3.1c — Test Mode, Settings, themes, V/I filter, touch harness

**Why:** v3.0 was confirmed on hardware (TP 200 Hz, V/T chips working), and four things
followed from bench use. The Calibrate screen only displayed compile-time constants and
did not earn a tile. Test Mode — ESC signal generation with configurable cycling — is the
board's namesake feature and was missing. The readings are noise-prone and needed a
tunable filter. And the theme did not read as dark, which turned into a long detour
(see the library entry below: the display was colour-inverted the whole time). On top of
that, touch responsiveness was reported fading after a minute or two of use, so the
build also carries the instrumentation to find out why.

**Changes**

- `EscOut.h/.cpp` — new. ESC servo signal on `PIN_ESC_SIG` (GP1, added to `Config.h`) via
  the RP2040 PWM slice: `analogWriteRange(periodUs)` makes the duty value the pulse width
  in microseconds. Manual set point 1000–2000 µs, frame period 5–20 ms, and a
  `millis()`-driven auto-cycle between two pulses with independent dwells. Output is a
  plain LOW pin until armed, disarming returns it to LOW rather than a zero-width frame,
  and any reset stops it.
- `Settings.h/.cpp` — new. Theme, filter index, ESC period and cycle parameters persisted
  by EEPROM emulation with magic, version, size and checksum, plus a range guard; anything
  implausible falls back to defaults. **Save is explicit** because an RP2040 flash commit
  halts both cores for a few ms. `settingsSave()` always stores the idle pulse regardless
  of the live set point, so a board never boots under throttle.
- `Sampler.h/.cpp` — linear weighted moving average (weights 1..N, newest heaviest) on V
  and I, selectable OFF/2/4/6/8/10/15/20 samples (26–263 ms at the 13.158 ms tick). Applied
  to the displayed numbers and the graph only. **Peaks stay raw**, screened by a
  physical-plausibility gate (≤160 A, ≤70 V) rather than a median: simulation showed
  median-of-3 discarding a genuine one-tick 100 A spike, which is exactly the event the
  instrument exists to catch. Energy integrates raw. Also core-1 health counters (tick
  overruns, longest tick).
- `Theme.h/.cpp` — the `COL_*` macros now dereference a runtime `Palette`, so switching
  theme is a pointer assignment plus repaint and no screen file changed. `PAL_DARK` is
  minimal-ink on pure black with bright borders; `PAL_CLASSIC` is navy built from real RGB
  (the v3.0 ground `0x0020` was the green LSB in RGB565, not blue).
- `ScreenTest.cpp` — new. Manual pulse with ±50/±10 steppers, frame period, cycle low/high
  pulse and dwells, start/stop. Status field doubles as the cycle countdown.
- `ScreenSettings.cpp` — new. Theme toggle, filter stepper (sample count in the box,
  window in ms on the caption line), calibration dump to serial (what `ScreenCal.cpp` used
  to draw — that file is deleted), Save, Dev Mode.
- `ScreenHome.cpp` — tiles are Live · Graph / Log · Test Mode · Settings; Dev moves under
  Settings. `ScreenDev.cpp` — theme toggle, the harness readouts (tap latency avg/max,
  worst frame, per-register drift, service max, tick health, `TP STALL` marker), and the
  crosshair now restores the chrome it drags across instead of scraping it.
- `Widgets.h/.cpp` — `drawStepBtn` draws +/- as filled bars sized from the box (a 5x7 glyph
  in a 44 px button was a speck), `drawStepperBox`, `t5Centered`, and the 4 px amber
  `escBar` along the top edge of every screen while the output is armed — placed there
  because every screen already spends its top corners and a motor warning must never be
  crowded out. Painted centrally in `paintScreen()` so a toast repaint cannot lose it.
- `Layout.h` — targets for Test (17) and Settings (7), a second Dev target, ESC bar height.
  `checkTargetOverlaps()` covers all six screens; verified offline as well: no hit-rect
  overlaps, no visual-rect overlaps, nothing off-screen.
- `RussoOne13/16/22.h` — regenerated. The originals had been built with a reduced
  character set: `+`, `/`, `,` and `%` were zero-width and silently dropped, which is why
  the step buttons first read "-50 -10 -10 -50". Regenerated with those glyphs; the
  parentheses were left out deliberately because they would have raised every font's
  height and shifted every layout. Heights are unchanged at 16/20/26.
- `Config.h` — `PIN_ESC_SIG 1`, `LCD_IPS 1`.
- `logging_current_meter_FABLE.ino` — settings load, theme apply, `escBegin()`, `escTick()`
  and `escBarTick()` in the loop, the once-per-second `[tp]` telemetry line (guarded by
  `availableForWrite()` so a stalled monitor can never block core 0), bench keys `d t L f
  e` alongside `r`, and a `built <date> <time>` banner line because every version in this
  project's history so far carries the same date.
- `README.md`, `firmware/README.md` — feature list, module layout, serial keys, the IPS
  inversion note, and the junction recipe for the library.

**Verification:** `arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero` against
arduino-pico 6.1.0: 102,828 bytes flash (4%), 17,160 bytes RAM (6%). Flashed and
bench-tested through v3.1b on the device: dark theme confirmed good, TP 200 Hz, UI 45 kHz,
tap latency 0 ms, service max 252 µs, one tick overrun in 39 minutes with a 7.9 ms worst
tick against a 13.2 ms budget. Test Mode ESC output not yet scoped. v3.1c itself (per-register
drift, navy palette, Test Mode re-spacing, crosshair repair) compiles clean and is not yet
flashed.

**Notes:** The touch fade has a lead. The register watchdog in this build found the
FT6336U's mode register `0xA4` not holding its value 47 times in 39 minutes and rewrote it
each time — consistent with the fade being controller state drift, and with the report that
touch "works a lot better" with the watchdog in. The per-register counters added here are
what will say whether all four registers move together (the part resetting — power or ESD on
the FPC) or A4 alone. Design decisions taken for v3.2 and not yet built: an ESC slider,
tap-to-jump anywhere; swipe-back recognised only when the swipe starts in dead space, so
buttons keep firing on the press edge.

### JCR_TouchScreen 1.1.0: fix IPS colour inversion, add controller watchdog and reinit

**Why:** Every colour this driver produced from v2.0 to v3.1a displayed as its
complement, and nobody noticed because a dark-on-black UI inverted to a light one that
merely looked "washed out". The old Arduino_GFX firmware constructed the panel with
`ips = true`, which sends `INVON`; this driver's init table ended with `INVOFF`. Every
palette judgement in that period was made on the inverted image. Separately, the
touch-fade investigation needed the driver to report on the controller's own health.

**Changes**

- `src/JCR_ST7796.h` — constructor gains `bool ips = true`; the init table ends with `0x21`
  when set, `0x20` otherwise. Default on because the module the library is documented
  against is IPS; TN panels pass false. Header comment explains the symptom.
- `src/JCR_FT6336.h/.cpp` — a register watchdog on the servicing core: every 500 ms it
  reads back `0x00`, `0x86`, `0x88`, `0xA4`, and any that no longer holds what `begin()`
  wrote is counted (`regDrift`, `driftByReg[4]`, `lastDriftReg`) and rewritten. Per-sample
  transaction timing (`serviceUsMax`, cleared per window by `resetServiceMax()`).
  `requestReinit()` re-runs `begin()` on the servicing core from a request flag, with the
  I2C bus brought up only once so a reinit cannot trip the core's pin-reassignment panic.
- `README.md` — IPS inversion section; the three new counters documented.
- `library.properties` — 1.1.0.

**Verification:** All three examples compile clean (70,076 / 70,492 / 70,644 bytes). On
hardware, the inversion fix is confirmed — black is black — and the watchdog produced its
first data (47 drifts on `0xA4` in 39 minutes, see the firmware entry above).


## v3.0 — 2026-09-08

### firmware: extract JCR_TouchScreen library and rebuild the UI as modules

**Why:** Touch had been unreliable across every earlier build — small targets
needed two or three presses, and touch appeared to die after a period of idling.
The cause turned out not to be the panel. Three things compounded: the FT6336U
ships in trigger mode, where `CTP_INT` only pulses on state *changes*, so a short
tap can be missed outright; touch was read from the drawing loop, so any slow
frame stretched the interval between reads until brief contacts landed and lifted
unseen; and monitor mode, enabled by default, drops the scan rate after ~30 s
idle. A minimal test firmware (`FABLE_DEV_TEST_SCREEN`) was written first to prove
the fix in isolation before the UI was rebuilt on it.

**Changes**

- `libraries/JCR_TouchScreen/` — new, 18 files. The reusable half of the old
  monolith: a raw-SPI ST7796 driver, the dual-core FT6336U touch engine and a
  bitmap text renderer, with no third-party graphics dependency.
  - `JCR_TFT.h/.cpp` — resolution- and rotation-agnostic base. Width and height
    are runtime state derived from the panel's native size and rotation, the
    scanline buffer is heap-allocated at `begin()` to the longest edge, and every
    primitive clips against live dimensions rather than compile-time macros.
  - `JCR_ST7796.h` — controller specifics behind three overrides (`writeInit`,
    `nativeSize`, `madctlFor`), so another controller or resolution is a small
    subclass rather than a fork. `rotationOffset()` exists for panels whose
    visible area is inset in the controller's address space.
  - `JCR_FT6336.h/.cpp` — the touch engine. Polling mode (`0xA4=0x00`), monitor
    mode forbidden (`0x86=0x00`), fastest scan period (`0x88=0x04`), fixed 200 Hz
    cadence, two-sample release confirmation, and full glitch counters. Events
    move through a lock-free single-producer ring inside the driver rather than
    the RP2040 hardware FIFO, which leaves that FIFO free for application use and
    lets touch be serviced from either core. **The library never claims a core** —
    `service()` is the primitive and the application decides where it runs.
  - `JCR_Text.h/.cpp` — one renderer for fixed-pitch and proportional bitmap
    fonts with integer scaling, replacing two near-duplicate renderers.
  - `fonts/Font5x7.*` — drawn glyph by glyph for this library, so it carries the
    same MIT licence as the code and no third-party font is redistributed.
  - `extras/make_fonts.py` — TTF to bitmap generator. `--mono` produces tabular
    figures, which is what makes a numeric field's cached redraw exact.
  - Three examples: `HelloScreen` (wiring proof), `DualCoreSampling` (core 1 doing
    its own periodic work *and* servicing touch, with a BLOCK button that stalls
    core 0 to demonstrate touch surviving it), `TouchReliabilityTest` (crosshair,
    16 px targets, every counter).
- `firmware/logging_current_meter_FABLE/` — the UI, rebuilt from one 2,360-line
  sketch into 21 files. The `.ino` is now ~190 lines of wiring; `Config.h` holds
  pins, bus rates and the calibration constants; `Layout.h` holds every pixel
  coordinate; `Sampler.*` the core-1 ADC and graph ring; `Widgets.*` the chrome
  and cached fields; `Screens.*` plus one `Screen*.cpp` per screen.
- `firmware/_archive/logging_current_meter_FABLE_v2.1.ino.txt` — the last
  bench-verified monolith, kept as a rollback. Deliberately not a `.ino` so the
  Arduino toolchain ignores it.
- `README.md` — repository layout gained `firmware/`, `libraries/` and this file;
  the "real firmware is not started" bullet replaced, since it is.
- `firmware/README.md` — the sketch table listed only `display_bringup`; it now
  covers all five sketches and the archive. Added a section on the new UI: the
  library dependency, the reasoning behind the dual-core touch arrangement, the
  four rules that keep it working, and the module layout. Corrected the toolchain
  section, which implied every sketch needs Arduino_GFX — the new build needs no
  third-party graphics library at all.
- `.gitignore` — excludes `Claude outputs/`, a working-session staging folder
  holding duplicate copies of files that also live in `firmware/` and
  `libraries/`, plus artefacts from unrelated tasks.

**Verification:** `arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero`
against arduino-pico 6.1.0, run in a Linux sandbox rather than on the development
machine. The sketch builds clean at 91,484 bytes flash (4%) and 16,344 bytes RAM
(6%); all three library examples build clean at 69,700 / 70,084 / 70,228 bytes.
**Not yet flashed or bench-tested on hardware** — v2.1 remains the last build
confirmed working on the device.

**Notes:** The refactor was reviewed line by line against v2.1 for transcription
drift; the ADC maths, formatters, graph autoscale and rendering, every layout
rect, and the dispatch and toast strings all match. That review also caught four
real defects, all fixed here: the `CTP_INT` edge counter was never re-attached
after the extraction, `resetStats()` was writing counters across cores instead of
raising a request on the owning core, a partially overhanging glyph cell could
program an address window the controller clamps and corrupt the display, and an
unmapped glyph was dropped without advancing the pen.

Two known cosmetic quirks, both accepted: the Dev Mode crosshair scrapes slivers
of static chrome while dragging (cleared on screen re-entry, not worth a repaint
per frame), and the 5x7 font is uppercase-only so "Wh"/"mA" render as "WH"/"MA".

`libraries/JCR_TouchScreen` must be copied or symlinked into
`Documents\Arduino\libraries\` before the sketch will build. It is staged inside
this repository so it is version-controlled alongside the project; if it is ever
published on its own, the README, LICENSE and `library.properties` are already
written for a standalone repository.
