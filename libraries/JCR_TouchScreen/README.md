# JCR_TouchScreen

Reliable capacitive touch and fast raw-SPI graphics for **ST7796 + FT6336U** panels on the
**RP2040**, with no external graphics library.

Built for the 3.5" 480×320 IPS SPI modules (lcdwiki MSP3525/MSP3526 and the many clones), but the
display driver is resolution- and rotation-agnostic, and adding another controller is a small
subclass.

By [Just 'Cuz Robotics](https://justcuzrobotics.com) · MIT licensed

---

## The problem this solves

These panels have a reputation for dropped taps: buttons that need two or three presses, small
targets that feel impossible, touch that "goes dead" if you leave the device alone for a while.
It usually isn't the hardware. Three things cause it, and this library fixes all three:

| Cause | What you see | Fix here |
|---|---|---|
| Controller ships in **trigger mode** (`0xA4 = 0x01`) — INT only pulses on state *changes* | Short taps missed entirely | Runs in **polling mode**; INT is counted as a diagnostic, never waited on |
| Touch is read from the main loop, so a slow frame stretches the gap between reads | Taps that land and lift between reads are lost | Sampled at a **fixed cadence** (200 Hz default) from whichever core you choose |
| **Monitor mode** left enabled (`0x86`) drops the scan rate after ~30 s idle | Touch feels dead until you tap a few times | Explicitly forbidden at init |

The result is that a tap is **registered** even when your UI is too busy to **react** to it —
press/release transitions queue as events and get drained when you're ready. Queue overflow is
counted, never silent.

## Quick start

```cpp
#include <JCR_TouchScreen.h>

JCR_ST7796 tft(5, 6, 7, 8);                 // CS, DC, RST, backlight (IPS: inversion on)
JCR_FT6336 touch(Wire1, 10, 11, 12, 13);    // I2C bus, SDA, SCL, RST, INT
JCR_Text   text(tft);

void setup() {
  pinMode(9, OUTPUT); digitalWrite(9, HIGH);  // park SD_CS — see wiring notes
  tft.setSPIPins(2, 3, 4);                    // SCK, MOSI, MISO
  tft.begin(40000000UL, /*rotation=*/1);      // 480x320 landscape
  touch.begin();
  touch.setMapping(320, 480, /*rotation=*/1); // panel's native size + rotation
  text.setFont(JCR_Font5x7);
  tft.fillScreen(0x0000);
  text.setScale(2);
  text.drawCentered(tft.width()/2, 20, "HELLO", 0x07FF, 0x0000);
}

void loop() {
  touch.service();                 // sample on schedule
  JCRTouchEvent ev;
  while (touch.popEvent(ev))       // drain press/release events
    if (ev.type == JCR_TOUCH_DOWN) tft.fillCircle(ev.x, ev.y, 6, 0xFFE0);
}
```

### IPS panels and colour inversion

The IPS variants of these modules need the controller's display inversion **on** (`INVON`), or
every colour comes out as its complement — black paints as white, dark blue as pale yellow. The
constructor's fifth argument `ips` (default **true**) handles it. If you painted black and got a
white screen, pass `false`: you have a TN panel.

## Wiring

Default example pinout (Waveshare RP2040-Zero). Any pins work — SPI0 needs a valid SCK/MOSI pair
for the RP2040's pin muxing.

| Signal | Pin | Notes |
|---|---|---|
| SCK / MOSI / MISO | 2 / 3 / 4 | display SPI |
| LCD CS / DC / RST | 5 / 6 / 7 | |
| Backlight | 8 | driven as a solid level — see below |
| **SD CS** | 9 | **park HIGH before anything else touches the bus** |
| CTP SDA / SCL | 10 / 11 | I2C1 |
| CTP RST / INT | 12 / 13 | INT optional (diagnostic only) |

Two things that will waste your afternoon if you skip them:

- **Park the module's SD card CS high.** These modules put the SD slot on the same FPC and share
  MISO. If SD_CS floats low the card fights the display for the bus and you get garbage or a dead
  panel. Set it `OUTPUT`/`HIGH` as the very first GPIO you touch.
- **Don't PWM the backlight** if the board also does analog measurement. `analogWrite`'s default
  ~1 kHz lands in the audio/measurement band and injects noise no calibration removes. Drive it
  as a solid level (`backlight(true)` does exactly that).

## Where to service touch

`service()` is the primitive. It self-paces — call it as often as you like and it samples only
when the next slot is due. **The library never claims a core**, so you decide where it runs:

**Simple sketches** — nothing else needs core 1, so hand it over:

```cpp
void setup1() { touch.begin(); touch.setMapping(320,480,1); JCRTouchCore1::attach(touch); }
void loop1()  { JCRTouchCore1::run(); }
```

**Your app needs core 1 too** — call `service()` yourself, including *inside* long jobs so they
can't starve it. This is the arrangement in `examples/DualCoreSampling`:

```cpp
void loop1() {
  touch.service();
  if (adcTickDue()) {
    for (uint8_t ch = 0; ch < 4; ch++) { readChannel(ch); touch.service(); }
  }
}
```

**Single core** — fine if `loop()` stays fast, but that's exactly the fragility this library
exists to remove. It's the right choice only for simple sketches.

Whichever you pick, keep these three rules and touch stays reliable as the project grows:

1. **Never block, print or draw on the servicing core.** Sampling only.
2. **The drawing core may be arbitrarily slow.** Drain events every pass; a slow frame delays
   your *reaction*, never the *registration*.
3. **Never repaint the whole screen every frame.** Paint static chrome once on entry, then redraw
   only the fields that changed. A full 480×320 fill is ~60 ms even at 40 MHz.

## Rotation and other resolutions

Nothing is fixed at compile time. `width()` and `height()` come from the panel's native size and
the current rotation; the scanline buffer is allocated at `begin()` to the longest edge; every
primitive clips against the live dimensions.

```cpp
tft.setRotation(0);   // 320x480 portrait
tft.setRotation(1);   // 480x320 landscape
touch.setMapping(320, 480, tft.rotation());   // keep touch in step
```

If the crosshair mirrors your finger, pass `invertX` / `invertY` to `setMapping()` — some modules
are wired differently. The crosshair under your finger is the ground truth; trust it over any
description of where a tap "should" have gone.

### Adding a controller

Copy `src/JCR_ST7796.h` and change three overrides:

```cpp
class JCR_ILI9488 : public JCR_TFT {
 protected:
  void    nativeSize(int16_t &w, int16_t &h) override { w = 320; h = 480; }
  uint8_t madctlFor(uint8_t rot) override { /* MY 0x80, MX 0x40, MV 0x20, BGR 0x08 */ }
  void    writeInit() override { /* your command table */ }
};
```

Override `rotationOffset()` too if the visible area is inset in the controller's address space
(common on small ST7789s). Every primitive already works off whatever those hooks report, so a
new resolution costs nothing extra.

## Text and fonts

`JCR_Text` renders fixed-pitch and proportional bitmap fonts through one path, with integer
scaling. `JCR_Font5x7` is built in.

```cpp
text.setFont(JCR_Font5x7);
text.setScale(3);
text.drawCentered(tft.width()/2, 40, "12.4V", 0xFFE0, 0x0000);
```

Text draws as **opaque cells** (foreground over background). That's deliberate: it means a
fixed-width numeric field can be updated by redrawing it, with no flicker and no erase step.

Generate your own from any TTF with `extras/make_fonts.py` (needs Pillow):

```
python make_fonts.py --font MyFont.ttf --size 28 --name MYFONT_28 --out MyFont28.h
```

Use `--mono` for tabular digits — worth it for any number that updates in place, since every
glyph then occupies the same cell and the field width never shifts.

> The built-in 5×7 glyphs were drawn from scratch for this library and carry the same MIT license
> as the code. If you generate a font from a third-party TTF, that font's license governs the
> result — check it before redistributing.

## Examples

| Example | What it shows |
|---|---|
| **HelloScreen** | Minimal bring-up. Run this first to prove wiring. |
| **DualCoreSampling** | Core 1 does its own periodic work *and* services touch. Hold BLOCK to stall core 0 by 50 ms/frame and watch the sample rate refuse to move. |
| **TouchReliabilityTest** | Live crosshair, raw vs mapped coordinates, targets down to 16 px, and every diagnostic counter. Use this to validate a new panel or debug a flaky one. |

## Diagnostics

`getStats()` returns counters worth putting on a debug screen:

| Counter | Meaning |
|---|---|
| `sampleHz` | Achieved `service()` rate. Should sit at your configured rate on every screen. |
| `i2cErrors` | Transaction failures — wiring or pull-ups. |
| `rangeGlitches` / `countGlitches` | Controller reported an impossible coordinate or contact count. |
| `dropouts` | A held contact briefly read as released and was bridged. A few is normal. |
| `overflows` | Event queue filled — your drawing core isn't draining often enough. |
| `intEdges` | INT pin activity. Diagnostic only; nothing depends on it. |
| `regDrift` / `lastDriftReg` | The servicing core reads the four configuration registers back every 2 s. Any that no longer holds what `begin()` wrote is counted, attributed, and rewritten. A part reverting to trigger or monitor mode on its own shows up here — and gets corrected. |
| `serviceUsMax` | Longest single sample transaction. A slow or stuck I2C bus becomes a number. `resetServiceMax()` clears it per window. |
| `reinits` | `requestReinit()` calls honoured — re-runs `begin()` on the servicing core, safe to call from the other one. A bench test for "has the controller lost its mind". |

**`lastGoodSampleMicros()`** (1.2.0) is the `micros()` of the last sample that was read and
published. It stops advancing when reads fail (I2C error, impossible contact count,
out-of-range point) or the servicing core stalls — in all of which `isDown()` keeps its last
value and no release event is queued. Anything safety-relevant held under a finger (a
dead-man throttle, a jog control) should treat "stale for more than a few samples" as a
release rather than trusting a frozen `isDown()`.

## Requirements

RP2040 with the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core (uses its
`SPIClassRP2040` pin setters and dual-core support). Only the Arduino `SPI` and `Wire` libraries
are needed beyond that.

## License

MIT — see `LICENSE`.
