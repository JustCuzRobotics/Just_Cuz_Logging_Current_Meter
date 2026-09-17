# Firmware

| Sketch | Purpose |
|---|---|
| `display_bringup/` | Full board diagnostic and calibration tool. Exercises all 20 usable GPIO, reports pass/fail per subsystem with net names, and fits the analog calibration constants |
| `logging_current_meter_FABLE/` | **The current UI (v3.2).** Live V/I/T/W, energy and run timer, autoscaled 5 s graph, Test Mode (ESC signal + auto-cycle), **SD logging with manual / cycle / current-threshold start and a USB CSV stream**, Settings with flash persistence, two themes, tunable V/I filter, touch diagnostics with a serial telemetry harness. One module per concern; built on `libraries/JCR_TouchScreen/` |
| `FABLE_DEV_TEST_SCREEN/` | Minimal touch-reliability test firmware. The quickest way to prove a panel and its touch mapping in isolation |
| `logging_current_meter_ui/` | Superseded predecessor (v1.7, Arduino_GFX). Kept as a reference until the new UI is bench-verified |
| `touch_dev_test/` | Earlier touch experiment. Superseded; not a reference |
| `_archive/` | Point-in-time copies of superseded firmware, kept as rollbacks. Not compiled — the extensions are deliberately not `.ino` |

**Datalogging to microSD and the USB stream are built as of v3.2** (compile-checked and
simulated on the host; not yet bench-verified). See *Logging* under
`logging_current_meter_FABLE` below. `tools/capture_stream.py` records the stream on a PC.

---

## `display_bringup`

**Everything reports over USB serial at 115200.** The display is treated as one more
subsystem under test — if the panel is dead, every other test still runs and still reports.
Do not rely on the screen to tell you anything.

> **USB power only.** Nothing in the 60 V current path, except during a deliberate voltage
> calibration. The sketch checks `V_PACK` at boot and shouts if it sees pack voltage.

### Toolchain

- **Board:** Arduino IDE → *"Raspberry Pi Pico/RP2040"* by **Earle Philhower**, board
  **Waveshare RP2040 Zero**
- **Library:** **"GFX Library for Arduino"** by *moononournation*. **Not Adafruit_GFX** —
  different library, different header, no ST7796 driver. Required by `display_bringup`
  and by the superseded `logging_current_meter_ui` only
- **Touch:** no library; the FT6336U is driven register-direct
- **Serial Monitor:** 115200, and **set the line ending to Newline** or no command you type
  will ever register

### Commands

| | |
|---|---|
| `a` / `A` | analog report, with expected values / streaming at 2 Hz |
| `b` | button report — idle level, press count, hold time, measured bounce |
| `c` | show the stored calibration constants |
| `s` | GPIO bridge / short test (unplug the ribbon first) |
| `k` | backlight ramp |
| `m` | MISO read-back, the `DESIGN.md` §11 test |
| `d` | display test pattern |
| `t` / `x` | touch controller scan and identity / coordinate stream |
| `g` | **live V/I graph — 5 s scrolling window, volts blue, amps red** |
| `e` / `l` | ESC signal output on GP1 / GP0↔ESC_SIG loopback |
| `n` / `v` / `i` | thermistor / voltage / current calibration |
| `r` / `?` | re-run the full sequence / help |

Streams stop on any key — an actual character, not a bare Enter.

### Calibration

Constants are **baked into the top of the sketch** and applied from boot, so they survive a
reflash. Each routine prints `#define` lines to paste back in. Press `c` to see what is
stored and which channels are genuinely calibrated.

Current state: voltage **fitted**, thermistor **fitted**, current **zero fitted / gain
nominal**. Full procedure and the reasoning behind each choice is in `DESIGN.md` §6.

Three things that will waste your time if you skip them:

- **Voltage needs two points with an offset**, not one through the origin. There is a real
  8.2 ADC count zero offset; ignoring it costs over 1 % at low pack voltages.
- **Fit the current gain at the highest current you can source.** The ±1.5 A nonlinearity
  floor is ±5 % of a 30 A fit but ±37 % of a 4 A one.
- **Do not use an ice bath for the thermistor.** Wet leads shunt it by ~340 kΩ — a 49 %
  error at 0 °C and 2 % at 100 °C. Room temperature plus boiling gives 77 °C of spread and
  works properly.

### Before first power-up on a new board

Board unpowered, DMM in continuity mode. All three of these cost an evening each the first
time. `DESIGN.md` §9 has the detail.

1. **Bridge `JP1`** — pad 2 (`DISP_VCC`) to pad 1 (`+5V`). It ships open and it is the
   display's only supply. Confirm pad 2 is *not* also bridged to pad 3.
2. **Use a Type A (same-side) FFC cable** for `J7`. The one in the display's box is Type B
   and reverses the pin order.
3. **Check `R11`–`R14` are fitted.** All four are 0 Ω placeholders in the SPI and backlight
   paths; a missing one is an open circuit, not a default.

### Known Rev A fault

**SW1 and SW2 are permanently shorted to GND** — the `SW_TS-1187A` footprint pad remap
assumed the wrong internal pairing. Everything is therefore driven by serial commands. Any
firmware built on this must not assume the buttons exist. `DESIGN.md` §9.

### Pin map

Mirrors `EXPECTED_MCU` in `generator/verify.py`. Verified against both that and the
module's own pin table.

| FPC | Module | Net | RP2040 |
|---|---|---|---|
| 1 | VCC | `DISP_VCC` | via `JP1` |
| 2 | GND | `GND` | — |
| 3 | LCD_CS | `LCD_CS` | GP5 |
| 4 | LCD_RST | `LCD_RST` | GP7 |
| 5 | LCD_RS | `LCD_RS` | GP6 |
| 6 | SDI (MOSI) | `SPI_MOSI` | GP3 via `R12` |
| 7 | SCK | `SPI_SCK` | GP2 via `R11` |
| 8 | LED | `DISP_LED` | GP8 via `R14` |
| 9 | SDO (MISO) | `SPI_MISO` | GP4 via `R13` |
| 10 | CTP_SCL | `CTP_SCL` | GP11 |
| 11 | CTP_RST | `CTP_RST` | GP12 |
| 12 | CTP_SDA | `CTP_SDA` | GP10 |
| 13 | CTP_INT | `CTP_INT` | GP13 |
| 14 | SD_CS | `SD_CS` | GP9 |

Analog: GP26 `I_SENSE`, GP27 `V_PACK`, GP28 `T_SENSE`, GP29 `V5_SENSE`.
Other: GP0 spare (J13 pad), GP1 `ESC_SIG_MCU`, GP14/GP15 buttons.

---

## `logging_current_meter_FABLE`

The working UI. **Uses no third-party graphics library** — the display, touch and text
stack lives in `libraries/JCR_TouchScreen/`, which must be on the Arduino library path
before this sketch will build:

```
arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero firmware/logging_current_meter_FABLE
```

Rather than copying the library into the sketchbook and forgetting to re-copy it, make the
sketchbook entry a junction to the repo copy — then every library edit is what the IDE
compiles:

```powershell
New-Item -ItemType Junction -Path "$HOME\Documents\Arduino\libraries\JCR_TouchScreen" `
         -Target "<repo>\libraries\JCR_TouchScreen"
```

**The panel is IPS and needs display inversion on.** `Config.h` sets `LCD_IPS 1`, which the
library turns into `INVON`. Without it every colour displays as its complement — black is
white — which is exactly how v2.0 through v3.1a looked, and why the themes made no sense
until v3.1b.

### Why touch is arranged the way it is

These panels are notorious for dropped taps, and the cause is not the hardware. The
FT6336U ships in trigger mode, where `CTP_INT` only pulses on state *changes*, so a short
tap can be missed outright; and reading touch from the drawing loop means any slow frame
stretches the gap between reads until brief contacts land and lift unseen. A third trap:
monitor mode is enabled by default and quietly drops the scan rate after ~30 s idle, which
reads as "touch goes dead if you leave it alone".

So the arrangement is inverted. **Core 1 owns touch and the ADC; core 0 owns the display.**
The controller runs in polling mode and is sampled at a fixed 200 Hz, interleaved *between*
the four ADC channel bursts so a ~5 ms acquisition can never delay a touch sample by more
than one channel. Press and release become queued events, so a slow frame delays the
*reaction* to a tap, never its *registration*.

Four rules keep that true as features are added. They are restated in the sketch header:

1. Core 1 never blocks, prints or draws — sampling and touch servicing only.
2. Core 0 may be arbitrarily slow; drain touch events every pass regardless.
3. Never repaint the whole screen per tick. Chrome once on entry, then only the fields
   that changed. A full-screen fill is ~60 ms even at 40 MHz.
4. Core 0 never touches the ADC; core 1 never touches SPI.

### Layout

One file per concern. `Config.h` holds the pin map, bus rates and **the calibration
constants** — that is the file to edit after running `display_bringup`'s `v`/`i`/`n`
routines (Settings → DUMP prints the current ones over serial). `Layout.h` holds every
pixel coordinate and touch target, so retargeting to another panel size is one file.
`Sampler.*` is the core-1 ADC, the V/I weighted-moving-average filter and the graph ring;
`Settings.*` the user settings and their EEPROM persistence (explicit Save only — a flash
write halts both cores for a few ms); `EscOut.*` the ESC servo signal on GP1 (the core's
Servo library — PIO, `writeMicroseconds()`, fixed 50 Hz — OFF at boot, always arms at idle
with a 2 s hold); `Logger.*` the SD
log, its triggers and the USB stream; `Version.h` the version string the banner and log
headers share; `Theme.*` the
two palettes behind the `COL_*` macros; `Widgets.*` the button chrome and cached fields;
`Screens.*` navigation, with one `Screen*.cpp` per screen.

### Serial keys (115200)

| key | does |
|---|---|
| `d` | once-per-second `[tp]` telemetry line: touch rate, UI rate, worst frame, tap latency, every touch counter, controller register drift per register, core-1 tick health |
| `r` | reset the touch counters |
| `t` | re-initialise the touch controller from core 1 |
| `L` | synthetic ~60 ms core-0 load per loop |
| `f` | filter 0 ↔ 20 samples |
| `e` | ESC output arm (idle 1000 µs, 2 s hold) / disarm, then prints the ESC state |
| `p` | print the ESC state — attached, set point, pulse on the pin, hold/cycle |
| `s` | USB CSV stream on/off (not saved — the LOG screen's SAVE persists it) |
| `g` | SD log start / stop |
| `m` | remount the SD card (runs the read-back test) |

**Every serial line that is not a CSV data row starts with `#`** as of v3.2, so a capture
can separate data from the banner, `[tp]` telemetry and `[log]` events by the first
character.

The `[tp]` line is the touch-investigation tool: `TP` dropping means core 1 is starved,
`lat` climbing with `TP` steady means core 0 is slow, `dn` not counting on a real tap means
the controller stopped reporting, and `drift=[a/b/c/d]` counts the controller's four
configuration registers found not holding their values (rewritten automatically).

### ESC output — the v3.1 timing bug

v3.1 drove GP1 with `analogWriteFreq(1e6/period)` + `analogWriteRange(period)`. **arduino-pico
clamps `analogWriteFreq()` to ≥ 100 Hz, silently**, so the 50 Hz frame ran at 100 Hz on a
20000-count range and every pulse was half width: "1000 µs idle" was 500 µs, "2000 µs"
was 1000 µs (any frame longer than 10 ms was scaled by 10000/period). Some ESCs reject a
500 µs pulse and never arm; others accept it as zero throttle. v3.2 uses arduino-pico's
bundled **Servo** library instead (PIO state machine, `writeMicroseconds()`), which is what
this should have used from the start. Its frame is fixed at 20 ms (50 Hz, a compile-time
constant in the library), so Test Mode's frame-period stepper is gone. It is attached as
`attach(pin, 1000, 2000, 1000)`: plain `attach(pin)` starts at 1500 µs. Check the waveform
on a scope before trusting a test — expect a 20 ms frame and a 1000 µs pulse after arming.

Arming now always emits 1000 µs for 2 s before the set point or a cycle applies, and the
set point resets to idle on arm. ESCs refuse to arm when their first frames carry
throttle.

### Logging (v3.2)

**LOG** tile on Home. All settings persist with **SAVE** (on the LOG screen; refused while
recording, because a flash commit pauses core 1). A red strip along the **bottom** edge of
every screen shows a log recording (solid) or CURRENT mode armed (dashed).

| Setting | Options |
|---|---|
| Mode (◀ ▶, wraps) | **MANUAL** (START/STOP or serial `g`) · **CYCLE** (Test Mode START CYCLE opens a log, STOP closes it) · **CURRENT** (starts when raw current > threshold for 3 ticks) |
| Rate | 76 / 38 / 15 / 7.6 / 1 Hz — decimations of the 13.158 ms tick (+ is faster) |
| Auto start above | 1–50 A, default **5 A** (CURRENT mode) |
| Duration | no limit, 1, 2, 3, 5, 10, 15 min — ends any log |

A stepper button that can go no further in its direction is greyed out.

- **Starting a log resets the energy counters and run timer**, so Live View and the file agree.
- CURRENT mode keeps **~0.5 s before the trigger** (negative `t_ms`), then re-arms only after
  the current has been below the threshold for 2 s — a run that outlasts its duration does
  not start a second file. With duration "no limit", a triggered log ends after 10 s below
  the threshold.
- An automatic start with no card mounted is **skipped, not mounted** — a failed mount
  blocks core 0 for ~2 s, which is not acceptable while a motor spins up. Insert the card and
  press REMOUNT SD (or serial `m`).
- Files are named `LOG_<n>_<MODE>_<V>V.CSV`, e.g. `LOG_07_CURRENT_22.4V.CSV`. `n` counts up
  from the highest number already on the card (no RTC, so `n` is the order), `MODE` is the
  log-mode setting, and `V` is the pack voltage at the start — for a current trigger, the
  resting voltage just before the load came on, not the sag. Not pre-allocated (a power pull would otherwise leave
  megabytes of junk after the data); 512 B block writes, synced every 2 s. Mounting runs a
  write/read-back test, because the card shares MISO with the LCD (`DESIGN.md` §11).

**SD file** — `#` header lines (build, trigger, mode, rate, filter, ESC period, which
calibration constants are nominal), then:

```
t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,mah,wh,esc_us
```

`t_ms` from log start · `_raw` is the instrument, `_filt` is what the screen showed ·
`w` = `v_raw × i_raw` · `t_c` blank on a thermistor fault · `mah`/`wh` since log start ·
`esc_us` is what was on GP1 that tick, 0 = off. A closing `# end` line carries the reason,
rows, totals, peaks and drop counts. pandas: `pd.read_csv(f, comment="#")`.

**USB stream** — same rows **without energy**, `t_ms` since boot:

```
t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,esc_us
```

**Graphs:** `python tools\log_analyzer.py` (needs `pip install pandas numpy matplotlib seaborn`)
opens a picker for a log file or a folder of logs and saves charts, a summary CSV and run
comparisons to a `Log Graphs` folder beside them.

A line that does not fit the USB buffer is dropped and counted (`USB DROP` on the LOG
screen), never waited for. On a PC: `pip install pyserial`, then
`python tools\capture_stream.py` — it finds the port, turns the stream on if needed, and
writes `meter_<date>_<time>.csv` with a `pc_time` column.

Fonts are generated locally by `make_fonts.py` from a TTF — Russo One here, which is SIL
OFL, so the generator ships rather than the font.

---

Per `memory.md` §3: `git pull` at the start of a session, `git push` at the end.
