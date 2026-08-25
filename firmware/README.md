# Firmware

Arduino firmware for the RC Logging Current Meter, Rev A hardware.

| Sketch | Purpose |
|---|---|
| `display_bringup/` | Hardware diagnostic — proves the FPC ribbon, ST7796U panel, FT6336U touch, backlight and buttons before any real firmware is written |

Nothing here reads the current, voltage or thermistor channels yet. See
`DESIGN.md` §11 for the intended architecture.

---

## `display_bringup` — first power-up diagnostic

This is a **diagnostic, not a demo**. Every stage prints `[ OK ]` or `[FAIL]`
over USB serial naming the net and FPC pin involved, so a dead conductor
identifies itself instead of leaving you looking at a blank panel.

> **USB power only.** Nothing in the 60 V current path while this runs. No pack,
> no ESC, nothing in J1/J3.

### Before you plug anything in

Board unpowered, DMM in continuity mode:

1. **Bridge `JP1`.** It ships open, and with all three pads open the display has
   **no power at all** — this is the single most likely reason for a dead screen
   on a first build.
   - `JP1` pad 2 is `DISP_VCC` → FPC pin 1.
   - Bridge **pad 2 to pad 1 (`+5V`)**. The module's spec calls for 5 V and its
     own ESP32 reference wiring runs VCC = 5 V against 3.3 V logic, which is
     exactly our case — the module has onboard level conversion.
   - Pad 3 (`+3V3`) also works, but the vendor notes *"when connected to 3.3 V
     the backlight brightness will be slightly dim."*
   - Confirm pad 2 is **not** shorted to both 1 and 3.
2. **Check `R11`–`R14` are actually fitted.** All four are 0 Ω placeholders in
   the SPI and backlight paths (`R11` SCK, `R12` MOSI, `R13` MISO, `R14`
   backlight). A missing one is an open circuit, not a default.
3. **Check ribbon orientation.** The supplied 14P FPC is a **reverse** cable —
   contacts on opposite faces at each end. With the cable seated, buzz `J7`
   pin 1 → module `VCC` and `J7` pin 14 → module `SD_CS`. If those two are
   swapped, the cable or the connector latch is backwards.
4. **No `DISP_VCC` → `GND` short.**

### Toolchain

- **Board:** Arduino IDE → Boards Manager → *"Raspberry Pi Pico/RP2040"* by
  **Earle Philhower**. Select **Waveshare RP2040 Zero**.
- **Library:** Library Manager → **"GFX Library for Arduino"** by
  *moononournation* (the `Arduino_GFX` library).
- **Touch:** no library. The FT6336U is driven register-direct in the sketch, so
  a failure points at a register read rather than someone else's abstraction.
- **Serial monitor:** 115200 baud.

Chosen over TFT_eSPI deliberately for bring-up: every pin is declared in the
`.ino`, so a blank screen can never be blamed on a stale `User_Setup.h`.
`DESIGN.md` §11 still targets TFT_eSPI for the real graphing UI — switch once
the hardware is proven, not before.

### Pin map

Mirrors `EXPECTED_MCU` / `EXPECTED_DISPLAY` in `generator/verify.py`. Verified
against both `verify.py` and the vendor's own pin table — all 14 agree, no
transposition. If you change one, change the other and re-run `verify.py`.

| FPC | Module pin | Net | RP2040 | Notes |
|---|---|---|---|---|
| 1 | VCC | `DISP_VCC` | — | via `JP1` |
| 2 | GND | `GND` | — | |
| 3 | LCD_CS | `LCD_CS` | GP5 | |
| 4 | LCD_RST | `LCD_RST` | GP7 | low = reset |
| 5 | LCD_RS | `LCD_RS` | GP6 | D/C: high = data |
| 6 | SDI (MOSI) | `SPI_MOSI` | GP3 | via `R12` |
| 7 | SCK | `SPI_SCK` | GP2 | via `R11` |
| 8 | LED | `DISP_LED` | GP8 | via `R14`, PWM |
| 9 | SDO (MISO) | `SPI_MISO` | GP4 | via `R13` — see §11 test below |
| 10 | CTP_SCL | `CTP_SCL` | GP11 | I²C1 |
| 11 | CTP_RST | `CTP_RST` | GP12 | low = reset |
| 12 | CTP_SDA | `CTP_SDA` | GP10 | I²C1 |
| 13 | CTP_INT | `CTP_INT` | GP13 | |
| 14 | SD_CS | `SD_CS` | GP9 | parked HIGH for the whole run |

`SW1` → GP14 (`BTN1`, MODE), `SW2` → GP15 (`BTN2`, ZERO/TARE). `R15`/`R16` are
marked not-fitted, so both use the RP2040's internal pull-ups and read active
low.

### What it does, in order

| Stage | Proves |
|---|---|
| Park `SD_CS` HIGH | First GPIO touched. The microSD can never be selected during LCD init |
| Backlight ramp | GP8 → `R14` → FPC 8 → module transistor, **with no SPI involved** |
| MISO read-back | The `DESIGN.md` §11 test — see below |
| `gfx->begin()` + colour bars, corner markers | SPI data path, rotation, full 480×320 window |
| I²C scan + FT6336U ID | FPC 10/11/12, and that the touch controller is alive |
| Touch crosshair, buttons | Coordinate mapping and `SW1`/`SW2` |

### The `DESIGN.md` §11 test

The microSD on this module shares MISO with the LCD, and cheap modules sometimes
fail to tri-state SD MISO when `SD_CS` is high — which corrupts LCD reads.
`R13` is the 0 Ω placeholder in that path for exactly this reason.

The sketch reads the ST7796U's `0xD3` ID register by **bit-banging** the bus
before `Arduino_GFX` claims `spi0`, with every pin as a plain GPIO. That depends
on nothing but copper.

- **`0x7796` comes back** → MISO is continuous end to end **and** the microSD is
  tri-stating correctly. §11 is answered, `R13` stays 0 Ω. Record this.
- **All `0x00` or all `0xFF`** → MISO stuck. Check `R13` is fitted, FPC 9, and
  ribbon seating.
- **Anything else** → inconclusive, not proof of a fault. Some level-shifted
  modules buffer MISO in a way that needs the panel initialised first. The panel
  can still work perfectly; only the read path is in question.

### Expected good run

```
backlight ramps 0 → full
five full-screen colour fills
eight vertical colour bars
corner markers in all four corners + white border + diagonals
serial: [ OK ] ST7796U ID read back  -- 0x7796 seen
serial: [ OK ] device at 0x38  -- FT6336U answering
serial: [ OK ] FT6336U identity  -- 0x64 + FocalTech vendor 0x11
status screen, then a green crosshair that tracks your fingertip
```

### Tuning after the first clean run

Everything below is at the top of `display_bringup.ino`:

- **`LCD_ROTATION`** — `1` or `3` for landscape. Use `3` if the image is upside
  down relative to how the enclosure will sit.
- **`TOUCH_INVERT_X` / `TOUCH_INVERT_Y`** — the touch panel reports in its
  native **320×480 portrait** frame while the display runs 480×320 landscape, so
  the axes are swapped in software. If the crosshair is mirrored relative to
  your finger, flip the corresponding one to `1`. The serial log prints raw and
  mapped coordinates side by side so you can see which axis is wrong.
- **`SPI_HZ`** — starts at a deliberately slow 12 MHz. Raise it (the panel will
  take 40 MHz+) only **after** a clean run, so a speed problem can never be
  confused with a wiring problem.

### Record the results

Two outcomes belong in `DESIGN.md` §11 once you have them:

1. The MISO tri-state answer — it decides whether `R13` stays 0 Ω or Rev B needs
   a change.
2. The working `LCD_ROTATION` and touch inversion constants.

### Buttons during the run

- **SW1** (GP14) cycles the backlight through 16 / 64 / 128 / 255.
- **SW2** (GP15) redraws the status screen. This is the tare button in the final
  firmware, so it is worth exercising.

---

Per `memory.md` §3: `git pull` at the start of a session, `git push` at the end.
Do not leave board or firmware edits unpushed.
