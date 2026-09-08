# Detailed Commit Timeline

The complete account of every commit, newest first. Commit messages stay short
and scannable; this file holds the full detail — what changed in each file, why,
how it was verified, and what remains open. Entries are grouped by version where
the project has one, otherwise by date, so a section can be lifted into release
notes with light editing.

<!-- Newest entries go directly below this line. -->

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
