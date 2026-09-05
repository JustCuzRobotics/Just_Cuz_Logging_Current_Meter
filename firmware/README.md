# Firmware

| Sketch | Purpose |
|---|---|
| `display_bringup/` | Full board diagnostic and calibration tool. Exercises all 20 usable GPIO, reports pass/fail per subsystem with net names, and fits the analog calibration constants |

The real logging firmware is **not started**. Its architecture, and the sampling
constraints measured on hardware, are in `DESIGN.md` §11.

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
  different library, different header, no ST7796 driver
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

Per `memory.md` §3: `git pull` at the start of a session, `git push` at the end.
