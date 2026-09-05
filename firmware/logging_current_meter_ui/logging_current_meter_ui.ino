/* ============================================================================
 * RC Logging Current Meter — Rev A touchscreen UI
 * ----------------------------------------------------------------------------
 *   VERSION 1.0          2026-09-05
 *
 *   This is the REAL touch-driven UI, ported 1:1 (pixel coordinates, colors,
 *   behavior) from the "UI-MIRROR" HTML mockup Seth reviewed and approved.
 *   It is a separate sketch from display_bringup.ino, not an edit to it —
 *   that one stays a serial-command diagnostic; this one is what actually
 *   runs on the panel. The two share no code (Arduino sketches don't span
 *   folders), so the handful of hardware-proven low-level routines below
 *   (ADC averaging, calibration math, the FT6336U touch driver, the ST7796
 *   display bring-up) are carried over verbatim from that sketch rather than
 *   re-derived.
 *
 *   SCOPE, on purpose:
 *     - The Calibrate screen is READ-ONLY. It shows the *baked* calibration
 *       constants below and their compile-time status flags — it does not
 *       re-implement display_bringup's interactive serial 'v'/'i'/'n'
 *       calibration prompts. Recalibrating still means flashing
 *       display_bringup.ino, running those commands there, and pasting the
 *       fitted numbers back in below (the exact workflow display_bringup's
 *       own banner already documents) — then reflashing THIS sketch.
 *     - The Log tile on Home is a disabled stub (dimmed, taps show a toast).
 *       No SD/logging code exists yet; DESIGN.md's future 250 kHz/SD pipeline
 *       is a separate, later project, not part of this UI build.
 *     - Touch here is tap-only (press/release edges for navigation and
 *       buttons) — nothing in the approved UI needs drag tracking, so this
 *       sketch polls the FT6336U's CTP_INT pin level directly each loop
 *       (cheap, no I2C) and only reads coordinates over I2C on an actual
 *       press edge. That's simpler than display_bringup's interrupt+50 ms
 *       poll-fallback scheme, which exists there because its crosshair test
 *       needs continuous coordinate tracking — we don't.
 *
 *   Hardware: Waveshare RP2040-Zero on the Logging_Current_Meter Rev A PCB.
 *   Display: lcdwiki MSP3526 / Hosyond 3.5" 480x320 IPS, ST7796U + FT6336U
 *   capacitive touch, on a shared 14-way 0.5 mm FPC. Two physical buttons
 *   (SW1/SW2) are dead from a footprint fault — every function here must be
 *   reachable by touch.
 *
 *   Toolchain: board "Waveshare RP2040 Zero" (arduino-pico core, Earle
 *   Philhower). Library "GFX Library for Arduino" by moononournation
 *   (Arduino_GFX — NOT Adafruit_GFX, different header).
 * ==========================================================================*/

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * Pin map — identical to display_bringup.ino
 * ========================================================================*/
#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_RS    6
#define PIN_LCD_RST   7
#define PIN_LCD_LED   8
#define PIN_SD_CS     9      /* must be parked HIGH — shares the FPC's MISO */
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13
#define PIN_I_SENSE  26      /* ADC0 — ACS770 current sensor              */
#define PIN_V_PACK   27      /* ADC1 — pack voltage divider               */
#define PIN_T_SENSE  28      /* ADC2 — NTC thermistor divider             */
#define PIN_V5_SENSE 29      /* ADC3 — +5V rail / 2, ratiometric reference */

/* ==========================================================================
 * Calibration constants — baked in, reused verbatim from display_bringup.ino.
 * Recalibrating means running that sketch's 'v'/'i'/'n' serial commands and
 * pasting fresh numbers in here (see the SCOPE note above), then reflashing.
 * ========================================================================*/
#define ADC_MAX          4095.0f
#define ADC_VREF            3.3f
#define G2_OVER_G1      0.755319f   /* cancels VCC/VREF in the current ratio */
#define ADC_AVG               64    /* oversample count; RP2040 ADC has DNL
                                      * artifacts near code boundaries       */

#define V_GAIN_CAL       0.01662778f   /* volts per ADC count                */
#define V_OFFSET_CAL    -0.13632f      /* volts                              */
#define V_CAL_VALID      1             /* both points fitted -> "FITTED"     */

#define I_QUIESCENT_CAL  0.105399f     /* ratio at 0 A — MEASURED            */
#define I_ZERO_VALID     1
#define I_SENS_CAL       0.00533200f   /* ratio per amp — NOMINAL, not yet
                                        * gain-fitted at a high current      */
#define I_GAIN_VALID     0

#define RV1_OHMS       100830.0f       /* MEASURED, FNIRSI DMM               */
#define NTC_B            3836.6f       /* FITTED, 21.1 + 100 C               */
#define NTC_R25         97988.0f       /* ANCHORED on the dry 22.1 C point   */

/* ==========================================================================
 * Display / touch constants — identical to display_bringup.ino
 * ========================================================================*/
#define LCD_ROTATION     1
#define LCD_W          480
#define LCD_H          320
#define SPI_HZ    12000000UL

#define TOUCH_NATIVE_W 320
#define TOUCH_INVERT_X   0    /* both proven correct on hardware at rotation 1 */
#define TOUCH_INVERT_Y   0

#define FT_ADDR           0x38
#define FT_REG_TD_STATUS  0x02

/* ==========================================================================
 * Palette — flat fills only, no gradients (the panel's low DPI would band
 * them). Hex source colors are noted so the intent stays legible; RGB565 is
 * the standard (R>>3)<<11 | (G>>2)<<5 | (B>>3) conversion.
 * Note: COL_GRID (#22404d) and COL_BOX_BORDER (#25404c) quantize to the same
 * RGB565 value — that's expected, not a typo.
 * ========================================================================*/
#define COL_BG              0x0041   /* #070a0e */
#define COL_GRID            0x2209   /* #22404d */
#define COL_TEXT            0xDF3D   /* #dce7ec — primary near-white text   */
#define COL_TEXT_HI         0xEF7E   /* #e8eef1 — labels/units, near-white  */
#define COL_VOLT            0x3F18   /* #3fe0c4 — voltage, teal            */
#define COL_AMP             0xFD84   /* #ffb020 — current, amber           */
#define COL_TEMP            0xFA73   /* #ff4f9a — temperature, magenta     */
#define COL_BOX_FILL        0x10E4   /* #131c22 */
#define COL_BOX_BORDER      0x2209   /* #25404c */
#define COL_BOX_PRESSED     0x1987   /* #1c3038 */
#define COL_DISABLED_FILL   0x08A3   /* #0f1418 */
#define COL_DISABLED_BORDER 0x2146   /* #232a30 */
#define COL_DISABLED_TEXT   0x4AAB   /* #4a545c — the one place dim text is
                                      * still correct: it signals "unavailable" */
#define COL_DANGER          0xFB4B   /* #ff6b5e — toast border/accent       */

/* ==========================================================================
 * GFX / touch objects — construction identical to display_bringup.ino
 * ========================================================================*/
Arduino_DataBus *bus = new Arduino_RPiPicoSPI(
    PIN_LCD_RS, PIN_LCD_CS, PIN_SCK, PIN_MOSI, PIN_MISO, spi0);
Arduino_GFX *gfx = new Arduino_ST7796(
    bus, PIN_LCD_RST, LCD_ROTATION, true /* IPS */);

/* ==========================================================================
 * Small shared geometry/UI types
 * ========================================================================*/
struct Rect { int16_t x, y, w, h; };

static bool hitTestRects(const Rect *rects, uint8_t n, int16_t x, int16_t y, int8_t *outId) {
  for (uint8_t i = 0; i < n; i++) {
    const Rect &r = rects[i];
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) { *outId = i; return true; }
  }
  return false;
}

/* Erases a FIXED-size field (sized for maxChars at the given text size) then
 * draws newText. A fixed erase width — rather than measuring the previous
 * string — sidesteps proportional-width erase bugs entirely; every caller
 * just reserves enough room for its field's worst-case text. Built-in GFX
 * font only: 6px advance x 8px cell per character, at integer text sizes. */
static void drawField(int16_t x, int16_t y, uint8_t maxChars, uint8_t textSize,
                       uint16_t fg, uint16_t bg, const char *text) {
  gfx->fillRect(x, y, maxChars * 6 * textSize, 8 * textSize, bg);
  gfx->setTextSize(textSize);
  gfx->setTextColor(fg);
  gfx->setCursor(x, y);
  gfx->print(text);
}

/* ==========================================================================
 * FT6336U capacitive touch — I2C1 (Wire1), reused verbatim from
 * display_bringup.ino: same reset pulse, same register writes (monitor mode
 * FORBIDDEN is the one that matters — the factory default allows it and it
 * silently drops the scan rate after 30 s idle), same 100kHz->400kHz switch.
 * ========================================================================*/
static bool ftReadBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)FT_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}
static void ftWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg); Wire1.write(val);
  Wire1.endTransmission();
}

static void touchInit() {
  pinMode(PIN_CTP_INT, INPUT_PULLUP);
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, HIGH); delay(10);
  digitalWrite(PIN_CTP_RST, LOW);  delay(20);
  digitalWrite(PIN_CTP_RST, HIGH); delay(300);

  Wire1.setSDA(PIN_CTP_SDA); Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin(); Wire1.setClock(100000); delay(50);

  ftWrite(0x00, 0x00);   /* normal working mode                */
  ftWrite(0xA5, 0x00);   /* P_ACTIVE                            */
  ftWrite(0x86, 0x00);   /* FORBID monitor mode — the main fix  */
  ftWrite(0x88, 0x04);   /* fastest allowed scan period          */
  ftWrite(0xA4, 0x01);   /* hold INT low while reporting        */
  Wire1.setClock(400000);
  delay(5);
}

/* Returns false for BOTH "no touch" and "I2C read failed" — a known
 * ambiguity inherited from display_bringup.ino and deliberately left as-is:
 * both cases should produce the same result here (no dispatch), so
 * disambiguating them would add risk for no behavioral gain. */
static bool touchRead(uint16_t *rx, uint16_t *ry) {
  uint8_t b[5];
  if (!ftReadBlock(FT_REG_TD_STATUS, b, 5)) return false;
  uint8_t n = b[0] & 0x0F;
  if (n == 0 || n > 2) return false;
  *rx = (uint16_t)(b[1] & 0x0F) << 8 | b[2];
  *ry = (uint16_t)(b[3] & 0x0F) << 8 | b[4];
  return true;
}

/* Raw touch-controller coords -> screen coords. At LCD_ROTATION==1:
 * screenX = rawY, screenY = (TOUCH_NATIVE_W-1) - rawX. TOUCH_INVERT_X/Y are
 * both 0 (proven correct on hardware) but kept as explicit no-op branches so
 * flipping either #define later needs no logic change. */
static void touchMap(uint16_t rx, uint16_t ry, int16_t *sx, int16_t *sy) {
  int16_t x = (int16_t)ry;
  int16_t y = (int16_t)(TOUCH_NATIVE_W - 1 - rx);
#if TOUCH_INVERT_X
  x = LCD_W - 1 - x;
#endif
#if TOUCH_INVERT_Y
  y = LCD_H - 1 - y;
#endif
  *sx = x; *sy = y;
}

/* ==========================================================================
 * ADC + calibration math — reused verbatim from display_bringup.ino.
 * ========================================================================*/
static uint16_t adcAvg(uint8_t pin) {
  uint32_t acc = 0;
  for (uint16_t i = 0; i < ADC_AVG; i++) { acc += analogRead(pin); delayMicroseconds(20); }
  return (uint16_t)(acc / ADC_AVG);
}
static float packVolts(uint16_t raw) { return V_GAIN_CAL * (float)raw + V_OFFSET_CAL; }

/* Both VCC and VREF cancel out of this ratio — DESIGN.md section 4.2 —
 * which is what makes the current reading independent of USB rail noise. */
static float iRatio(uint16_t rawI, uint16_t raw5) {
  if (raw5 < 100) return 0.0f;
  return (float)rawI / (float)raw5 * G2_OVER_G1;
}

/* Thermistor: NTC divider through RV1_OHMS, Steinhart-Hart B-parameter form.
 * x > 0.97 = open circuit (no NTC), x < 0.02 = shorted to ground. Returns
 * false (fault) so the caller can show "ERR" instead of a bogus reading —
 * the mockup never had to handle this since it was simulated; real hardware
 * should degrade honestly instead of printing a nonsense temperature. */
static bool tempFromRawC(uint16_t rawT, float *outC) {
  float x = rawT / ADC_MAX;
  if (x > 0.97f || x < 0.02f) return false;
  float rntc = RV1_OHMS * x / (1.0f - x);
  *outC = 1.0f / (1.0f / 298.15f + logf(rntc / NTC_R25) / NTC_B) - 273.15f;
  return true;
}

/* ==========================================================================
 * Fixed-point unit convention (deliberate — see the project plan for the
 * full rationale): pixel coordinates, indices, timers and sample counts are
 * plain integers throughout. Physical quantities are integers TOO, once
 * converted, at one of these resolutions:
 *   centiamps   = amps * 100        (int16_t; 0..~20000 covers 0..200 A)
 *   centivolts  = volts * 100       (int16_t; 0..~9000 covers 0..90 V)
 *   centidegc   = celsius * 100     (int16_t; -4000..20000 covers -40..200 C)
 *   milliwatts  = watts * 1000      (int32_t; centiwatts alone would
 *                                    overflow int16_t at 150 A x 65 V)
 * float is reserved for: the calibration math above (inherently fractional),
 * and energy integration (Wh/mAh) — that accumulation runs once per ~13 ms
 * sample tick, not per pixel, so the software-float cost is irrelevant, and
 * float32's precision is nowhere near a limiting factor at the Wh magnitudes
 * this device sees, even across a multi-hour test.
 * A sentinel marks "no valid reading" (thermistor fault) at this resolution. */
#define FIXED_INVALID  ((int16_t)-32768)

/* ==========================================================================
 * Cross-core shared state.
 *
 * Ownership rule that makes this safe WITHOUT a mutex: every field below is
 * written by core 1 only and read by core 0 only. A 32-bit-aligned int32/
 * float or a 16-bit uint16/int16 store is a single atomic bus transaction on
 * this part, so core 0 can never see a torn value — at worst it reads last
 * tick's number instead of this tick's, which is invisible at human UI
 * refresh rates. The three cmd* flags are the one place core 0 writes:
 * core 0 only ever sets a flag TRUE, core 1 only ever sets it back to FALSE
 * after acting on it — a single writer per direction, so no read-modify-
 * write race is possible either way.
 * ========================================================================*/
struct SampleState {
  int16_t  centiamps, centivolts, centidegc;
  int32_t  milliwatts;

  float    energyWh, energyMah;                 /* the deliberate float exception */
  bool     mark2Captured, mark3Captured;
  float    mark2Wh, mark2Mah, mark3Wh, mark3Mah;

  uint32_t runElapsedMs;

  int16_t  peakCentiamps, peakVAtPeakCentivolts, peakCentidegc;
  int32_t  peakMilliwatts;

  bool     tempFault;       /* true = thermistor open/shorted, ignore centidegc */
};
volatile SampleState gState;

/* core0 -> core1 command flags (see ownership note above) */
volatile bool gCmdTare = false;
volatile bool gCmdResetEnergyTimer = false;
volatile bool gCmdResetPeaks = false;

volatile bool gCoreReady = false;   /* set true at the end of core0's setup() */

/* ==========================================================================
 * Graph ring buffer — core1 writes, core0 reads. Same "aligned atomic store"
 * reasoning applies; gRingHead follows the exact convention proven in
 * display_bringup.ino's grHead: it is the index of the NEWEST sample, and
 * writing advances it AFTER the data is stored, never before.
 * 380 columns = the graph plot's actual pixel width (see PLOT_* below), one
 * sample per column over the trailing 5 s window: 5000/380 = 13.158 ms/tick.
 * Storage: 380 columns x 3 channels x 2 bytes = 2.3 KB, trivial against the
 * RP2040's 264 KB SRAM.
 * ========================================================================*/
#define GRAPH_COLS   380
#define TICK_US      13158UL

int16_t gRingI[GRAPH_COLS], gRingV[GRAPH_COLS], gRingT[GRAPH_COLS];
volatile uint16_t gRingHead = 0;

/* ==========================================================================
 * CORE 1 — the only core that ever touches the ADC. Runs continuously from
 * boot regardless of which screen is showing, exactly like the mockup's
 * simTick(): peaks/energy/timer/tare/graph history all keep accumulating in
 * the background whether Home, Live View, Graph or Calibrate is on screen.
 * ========================================================================*/
static float gTareOffsetA = 0.0f;   /* runtime-only zero offset; never
                                     * touches I_QUIESCENT_CAL (see Tare) */

void setup1() {
  while (!gCoreReady) { /* wait for core0's setup() to finish display/touch init */ }
}

/* One 13.158 ms tick: sample all four channels, convert, tare, integrate
 * energy, track peaks, service the estimated-energy freeze marks, apply any
 * pending command flag, and write one graph ring-buffer column. */
static void sampleTick() {
  uint16_t rawI = adcAvg(PIN_I_SENSE);
  uint16_t raw5 = adcAvg(PIN_V5_SENSE);
  uint16_t rawV = adcAvg(PIN_V_PACK);
  uint16_t rawT = adcAvg(PIN_T_SENSE);

  float ratio   = iRatio(rawI, raw5);
  float ampsRaw = (ratio - I_QUIESCENT_CAL) / I_SENS_CAL;

  /* ---- Tare: zero the CURRENT reading only, without touching the baked
   * calibration. Sanity-checked against the same ~0.1 nominal quiescent
   * ratio display_bringup's own tare step uses. */
  if (gCmdTare) {
    if (ratio > 0.05f && ratio < 0.15f) gTareOffsetA = -ampsRaw;
    gCmdTare = false;
  }
  float amps = ampsRaw + gTareOffsetA;
  if (amps < 0.0f) amps = 0.0f;

  float volts = packVolts(rawV);
  float tempC; bool tempOk = tempFromRawC(rawT, &tempC);
  float watts = volts * amps;

  int16_t centiamps  = (int16_t)lroundf(amps  * 100.0f);
  int16_t centivolts = (int16_t)lroundf(volts * 100.0f);
  int16_t centidegc  = tempOk ? (int16_t)lroundf(tempC * 100.0f) : FIXED_INVALID;
  int32_t milliwatts = (int32_t)lroundf(watts * 1000.0f);

  gState.centiamps  = centiamps;
  gState.centivolts = centivolts;
  gState.centidegc  = centidegc;
  gState.milliwatts = milliwatts;
  gState.tempFault  = !tempOk;

  /* ---- energy integration (the deliberate float exception — see the
   * fixed-point convention note above). dt is the actual measured tick
   * interval, not an assumed constant, so drift in the loop timing doesn't
   * bias the accumulated total. */
  static uint32_t lastTickUs = 0;
  uint32_t nowUs = micros();
  uint32_t dtUs = lastTickUs ? (nowUs - lastTickUs) : (uint32_t)TICK_US;
  lastTickUs = nowUs;
  const float K_WH_PER_US = 1.0f / 3600000000.0f;   /* Wh per (W * us) */
  gState.energyWh  += watts * (float)dtUs * K_WH_PER_US;
  gState.energyMah += amps * 1000.0f * (float)dtUs * K_WH_PER_US;

  gState.runElapsedMs += dtUs / 1000UL;

  /* ---- peaks. peakVAtPeakCentivolts is written BEFORE peakMilliwatts each
   * tick (fixed order) so core0 can never observe a new peak power paired
   * with a stale sag voltage — at worst both are one tick stale together. */
  if (centiamps > gState.peakCentiamps) gState.peakCentiamps = centiamps;
  if (milliwatts > gState.peakMilliwatts) {
    gState.peakVAtPeakCentivolts = centivolts;
    gState.peakMilliwatts = milliwatts;
  }
  if (!tempOk) { /* leave peakCentidegc as-is on a faulted reading */ }
  else if (centidegc > gState.peakCentidegc) gState.peakCentidegc = centidegc;

  /* ---- estimated-energy-needed freeze marks, at 2 and 3 minutes elapsed */
  if (!gState.mark2Captured && gState.runElapsedMs >= 120000UL) {
    gState.mark2Wh = gState.energyWh; gState.mark2Mah = gState.energyMah;
    gState.mark2Captured = true;
  }
  if (!gState.mark3Captured && gState.runElapsedMs >= 180000UL) {
    gState.mark3Wh = gState.energyWh; gState.mark3Mah = gState.energyMah;
    gState.mark3Captured = true;
  }

  /* ---- Peak Reset: I/P/Vsag -> 0, but T resets to the CURRENT reading —
   * 0 C is not a sensible floor for a peak that starts already above it. */
  if (gCmdResetPeaks) {
    gState.peakCentiamps = 0;
    gState.peakMilliwatts = 0;
    gState.peakVAtPeakCentivolts = 0;
    gState.peakCentidegc = tempOk ? centidegc : FIXED_INVALID;
    gCmdResetPeaks = false;
  }

  /* ---- Energy Reset and Timer Reset are the SAME combined action: energy,
   * elapsed time and both freeze marks all clear together. */
  if (gCmdResetEnergyTimer) {
    gState.energyWh = 0.0f; gState.energyMah = 0.0f;
    gState.runElapsedMs = 0;
    gState.mark2Captured = gState.mark3Captured = false;
    gCmdResetEnergyTimer = false;
  }

  /* ---- graph ring buffer: advance head AFTER storing, never before, so
   * core0 can never see a half-written "newest" slot. On a thermistor
   * fault, carry forward the last good T sample rather than plotting a
   * fault sentinel as if it were a real reading. */
  int16_t tForRing = tempOk ? centidegc : gRingT[gRingHead];
  uint16_t h = (gRingHead + 1) % GRAPH_COLS;
  gRingI[h] = centiamps;
  gRingV[h] = centivolts;
  gRingT[h] = tForRing;
  gRingHead = h;
}

void loop1() {
  static uint32_t next = 0;
  if (!next) next = micros();
  if ((int32_t)(micros() - next) < 0) return;
  next += TICK_US;                              /* += , so it catches up   */
  if ((int32_t)(micros() - next) > (int32_t)TICK_US * 4)
    next = micros() + TICK_US;                  /* too far behind — resync */
  sampleTick();
}

/* ==========================================================================
 * CORE 0 — touch, screen state machine, and all drawing. Never touches the
 * ADC. Screens are flat (Home + 3 subscreens, every Back returns to Home —
 * no navigation stack needed).
 * ========================================================================*/
enum ScreenId { SCR_HOME, SCR_LIVE, SCR_GRAPH, SCR_CAL };
ScreenId gScreen = SCR_HOME;

static void paintHomeOnce();          /* Home has nothing that changes per-tick */
static void paintLiveOnce(), updateLiveTick();
static void paintGraphOnce(), updateGraphTick();
static void paintCalOnce();           /* Calibrate's values are compile-time constants */

static void paintScreen(ScreenId s) {
  switch (s) {
    case SCR_HOME:  paintHomeOnce();  break;
    case SCR_LIVE:  paintLiveOnce();  break;
    case SCR_GRAPH: paintGraphOnce(); break;
    case SCR_CAL:   paintCalOnce();   break;
  }
}
static void goTo(ScreenId s) { gScreen = s; paintScreen(s); }

/* ---------------------------------------------------------------- toast --
 * Bottom-center, timed, non-blocking. There's no framebuffer to save/
 * restore pixels under it, so on expiry it simply re-paints the current
 * screen's chrome from scratch (paintXOnce is idempotent and cheap — a
 * handful of rect/line/text draws, never a full-frame fill) instead of
 * tracking exactly which pixels it covered. */
#define TOAST_MS  1600
static char gToastMsg[40];
static uint32_t gToastUntil = 0;

static void showToast(const char *msg) {
  strncpy(gToastMsg, msg, sizeof(gToastMsg) - 1);
  gToastMsg[sizeof(gToastMsg) - 1] = 0;
  gToastUntil = millis() + TOAST_MS;
  int16_t w = (int16_t)strlen(gToastMsg) * 6 + 16;
  int16_t x = (LCD_W - w) / 2, y = LCD_H - 38;
  gfx->fillRoundRect(x, y, w, 22, 4, 0x1010 /* dark toast bg */);
  gfx->drawRoundRect(x, y, w, 22, 4, COL_DANGER);
  gfx->setTextSize(1); gfx->setTextColor(0xFBEF /* #ffd7d1-ish */);
  gfx->setCursor(x + 8, y + 7); gfx->print(gToastMsg);
}
static void updateToast() {
  if (gToastUntil && (int32_t)(millis() - gToastUntil) >= 0) {
    gToastUntil = 0;
    paintScreen(gScreen);   /* repaint chrome + force a full value refresh */
  }
}

/* ------------------------------------------------------------ touch poll --
 * Tap-only interaction (nothing in the approved UI needs drag tracking), so
 * this polls CTP_INT's level directly every loop() iteration — cheap, no
 * I2C — and only reads coordinates over I2C on an actual press edge. That's
 * simpler than an interrupt+poll-fallback scheme, which display_bringup
 * needs only because its crosshair diagnostic tracks a finger continuously. */
static int8_t hitTestScreen(ScreenId s, int16_t x, int16_t y);       /* per-screen, below */
static void dispatch(ScreenId s, int8_t id);                        /* per-screen, below */
static void setPressedVisual(ScreenId s, int8_t id, bool pressed);   /* per-screen, below */

static int8_t gPressedId = -1;

static void pollTouch() {
  static bool prevDown = false;
  bool down = (digitalRead(PIN_CTP_INT) == LOW);

  if (down && !prevDown) {
    uint16_t rx, ry;
    if (touchRead(&rx, &ry)) {
      int16_t sx, sy; touchMap(rx, ry, &sx, &sy);
      int8_t id = hitTestScreen(gScreen, sx, sy);
      if (id >= 0) {
        gPressedId = id;
        setPressedVisual(gScreen, id, true);
        dispatch(gScreen, id);
      }
    }
  } else if (!down && prevDown) {
    if (gPressedId >= 0) { setPressedVisual(gScreen, gPressedId, false); gPressedId = -1; }
  }
  prevDown = down;
}

/* ==========================================================================
 * setup() / loop()
 * ========================================================================*/
#define LIVE_FRAME_MS   50     /* ~20 Hz — fast enough the .mmm timer digits
                                * read as live, not stepped                 */
#define GRAPH_FRAME_MS  33     /* ~30 Hz — smooth to the eye, comfortably
                                * inside the measured partial-update budget */

void setup() {
  /* SD_CS must never float low — first GPIO touched, same as display_bringup */
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_LCD_LED, OUTPUT); digitalWrite(PIN_LCD_LED, HIGH);  /* solid — never PWM'd */

  analogReadResolution(12);
  Serial.begin(115200);

  gfx->begin(SPI_HZ);
  touchInit();

  Serial.println(F("Logging Current Meter UI  v1.0"));

  goTo(SCR_HOME);
  gCoreReady = true;   /* release core1 last, now that display/touch are up */
}

void loop() {
  pollTouch();

  static uint32_t lastLiveFrame = 0, lastGraphFrame = 0;
  uint32_t now = millis();
  switch (gScreen) {
    case SCR_LIVE:
      if (now - lastLiveFrame >= LIVE_FRAME_MS) { lastLiveFrame = now; updateLiveTick(); }
      break;
    case SCR_GRAPH:
      if (now - lastGraphFrame >= GRAPH_FRAME_MS) { lastGraphFrame = now; updateGraphTick(); }
      break;
    default: break;   /* Home and Calibrate have nothing that changes per-tick */
  }

  updateToast();
}

/* ==========================================================================
 * HOME — 2x2 tile grid, region (20,44)-(460,300), 14px gaps. Live View and
 * Graph navigate; Log is a disabled stub (no SD/logging code exists yet);
 * Calibrate navigates. Geometry is ported pixel-for-pixel from the mockup's
 * .home-grid (which was itself built 1:1 at 480x320).
 * ========================================================================*/
enum { HOME_LIVE, HOME_GRAPH, HOME_LOG, HOME_CAL, HOME_N };
static const Rect HOME_RECTS[HOME_N] = {
  { 20,  44, 213, 121 },   /* Live View  */
  { 247,  44, 213, 121 },  /* Graph      */
  { 20, 179, 213, 121 },   /* Log — disabled */
  { 247, 179, 213, 121 },  /* Calibrate  */
};
static const char *HOME_LABEL[HOME_N] = { "LIVE VIEW", "GRAPH", "LOG", "CALIBRATE" };
static const char *HOME_SUB[HOME_N]   = { "V I T W energy", "scaled plot, peaks", "start/stop", "view constants" };
static const bool  HOME_DISABLED[HOME_N] = { false, false, true, false };

/* Small vector-drawn glyphs, standing in for the mockup's Unicode icons
 * (bullet/wave/square/gear) — the built-in GFX font can't render those. */
static void drawHomeGlyph(uint8_t id, int16_t cx, int16_t cy, uint16_t color) {
  switch (id) {
    case HOME_LIVE:  gfx->fillCircle(cx, cy, 7, color); break;
    case HOME_GRAPH:
      gfx->drawLine(cx - 12, cy + 4, cx - 4, cy - 6, color);
      gfx->drawLine(cx - 4, cy - 6, cx + 4, cy + 2, color);
      gfx->drawLine(cx + 4, cy + 2, cx + 12, cy - 8, color);
      break;
    case HOME_LOG:   gfx->drawRect(cx - 7, cy - 7, 14, 14, color); break;
    case HOME_CAL:
      gfx->drawCircle(cx, cy, 7, color);
      for (uint8_t k = 0; k < 6; k++) {
        float a = k * 3.14159f / 3.0f;
        int16_t x0 = cx + (int16_t)(cosf(a) * 9), y0 = cy + (int16_t)(sinf(a) * 9);
        int16_t x1 = cx + (int16_t)(cosf(a) * 12), y1 = cy + (int16_t)(sinf(a) * 12);
        gfx->drawLine(x0, y0, x1, y1, color);
      }
      break;
  }
}

static void paintHomeTile(uint8_t id, bool pressed) {
  const Rect &r = HOME_RECTS[id];
  bool disabled = HOME_DISABLED[id];
  uint16_t fill = disabled ? COL_DISABLED_FILL : (pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  uint16_t border = disabled ? COL_DISABLED_BORDER : (pressed ? COL_VOLT : COL_BOX_BORDER);
  uint16_t textCol = disabled ? COL_DISABLED_TEXT : COL_TEXT;
  uint16_t glyphCol = disabled ? COL_DISABLED_TEXT : COL_VOLT;

  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  drawHomeGlyph(id, r.x + r.w / 2, r.y + 34, glyphCol);

  const char *label = HOME_LABEL[id];
  int16_t lw = (int16_t)strlen(label) * 12;   /* textSize(2): 12px advance */
  gfx->setTextSize(2); gfx->setTextColor(textCol);
  gfx->setCursor(r.x + (r.w - lw) / 2, r.y + 56);
  gfx->print(label);

  const char *sub = HOME_SUB[id];
  int16_t sw = (int16_t)strlen(sub) * 6;
  gfx->setTextSize(1); gfx->setTextColor(disabled ? COL_DISABLED_TEXT : COL_TEXT_HI);
  gfx->setCursor(r.x + (r.w - sw) / 2, r.y + 80);
  gfx->print(sub);
}

static void paintHomeOnce() {
  gfx->fillScreen(COL_BG);
  for (uint8_t i = 0; i < HOME_N; i++) paintHomeTile(i, false);
}

static int8_t homeHitTest(int16_t x, int16_t y) {
  int8_t id; return hitTestRects(HOME_RECTS, HOME_N, x, y, &id) ? id : (int8_t)-1;
}
static void homeSetPressed(int8_t id, bool pressed) { paintHomeTile((uint8_t)id, pressed); }
static void homeDispatch(int8_t id) {
  if (HOME_DISABLED[id]) { showToast("Not implemented in this build"); return; }
  switch (id) {
    case HOME_LIVE:  goTo(SCR_LIVE);  break;
    case HOME_GRAPH: goTo(SCR_GRAPH); break;
    case HOME_CAL:   goTo(SCR_CAL);   break;
  }
}

/* ==========================================================================
 * Shared value-formatting helpers. Fixed-point fields format with plain
 * integer division/modulo — no float touched just to print a number.
 * ========================================================================*/
static void fmtCentiUnit(int16_t centi, const char *unit, char *buf, size_t n) {
  if (centi == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  snprintf(buf, n, "%d.%02d%s", centi / 100, abs(centi % 100), unit);
}
static void fmtDegC(int16_t centidegc, char *buf, size_t n) {
  if (centidegc == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  snprintf(buf, n, "%dC", centidegc / 100);
}
static void fmtWatts(int32_t milliwatts, char *buf, size_t n) {
  snprintf(buf, n, "%ldW", (long)(milliwatts / 1000));
}
static void fmtTimer(uint32_t ms, char *buf, size_t n) {
  uint32_t totalSec = ms / 1000, m = totalSec / 60, s = totalSec % 60, msPart = ms % 1000;
  snprintf(buf, n, "%02lu:%02lu.%03lu", (unsigned long)m, (unsigned long)s, (unsigned long)msPart);
}
static void fmtMark(bool captured, float wh, float mah, char *buf, size_t n) {
  if (!captured) snprintf(buf, n, "---");
  else snprintf(buf, n, "%.2fWh %.0fmA", wh, mah);
}

/* ==========================================================================
 * LIVE VIEW — Back + a top-right run timer/reset, a 4-column V/I/T/P stat
 * row, an Energy section (Wh/mAh + one reset), an "Estimated Energy Needed"
 * section (2-min/3-min frozen marks, no reset of its own), and a bottom
 * Tare/Peak-Reset action row. Geometry ported from the mockup's .live-body.
 * ========================================================================*/
static const Rect LIVE_BACK       = {   8,   8,  52, 30 };
static const Rect LIVE_TIMER_BOX  = { 348,   8,  86, 30 };
static const Rect LIVE_TIMER_RST  = { 440,   8,  32, 30 };
static const Rect LIVE_STAT[4]    = { {8,42,110,62}, {126,42,110,62}, {244,42,110,62}, {362,42,110,62} };
static const Rect LIVE_ENERGY_RST = {   8, 134,  32, 32 };
static const Rect LIVE_ENERGY[2]  = { { 46, 134, 210, 32 }, { 262, 134, 210, 32 } };
static const Rect LIVE_EST[2]     = { {  8, 196, 229, 32 }, { 243, 196, 229, 32 } };
static const Rect LIVE_TARE       = {   8, 252, 226, 52 };
static const Rect LIVE_PEAK_RST   = { 246, 252, 226, 52 };

enum { LIVE_BTN_BACK, LIVE_BTN_TIMER_RST, LIVE_BTN_ENERGY_RST, LIVE_BTN_TARE, LIVE_BTN_PEAK_RST, LIVE_BTN_N };
static const Rect  *LIVE_RECTS[LIVE_BTN_N] = { &LIVE_BACK, &LIVE_TIMER_RST, &LIVE_ENERGY_RST, &LIVE_TARE, &LIVE_PEAK_RST };

static const char *LIVE_STAT_LABEL[4] = { "VOLT", "CURRENT", "TEMP", "POWER" };
static const uint16_t LIVE_STAT_COLOR[4] = { COL_VOLT, COL_AMP, COL_TEMP, COL_TEXT };

static void drawBackBtn(const Rect &r, bool pressed) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(r.x + 10, r.y + 11); gfx->print(F("< HOME"));
}
/* Compact back button (Graph screen only) — same width as the icon buttons
 * on the header/peak rows either side of it, so all three rows' leftmost
 * column lines up, matching the mockup's explicit column-alignment goal. */
static void drawBackBtnCompact(const Rect &r, bool pressed) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(r.x + r.w / 2 - 3, r.y + r.h / 2 - 4); gfx->print('<');
}
static void drawIconBtn(const Rect &r, bool pressed, char glyph) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(r.x + r.w / 2 - 3, r.y + r.h / 2 - 4); gfx->print(glyph);
}
static void drawActionBtn(const Rect &r, bool pressed, const char *label) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 7, pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 7, COL_BOX_BORDER);
  int16_t lw = (int16_t)strlen(label) * 12;
  gfx->setTextSize(2); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(r.x + (r.w - lw) / 2, r.y + r.h / 2 - 8);
  gfx->print(label);
}
/* Mini-stat box: a bordered rect with a small label and a value line below
 * it — used for the energy row, the estimated-energy row, and (on Graph)
 * the peak row. Chrome (border+label) is drawn once by the caller's paint
 * function; only the value field is redrawn per tick via drawField(). */
static void drawMiniStatChrome(const Rect &r, const char *label) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(r.x + 6, r.y + 4); gfx->print(label);
}
static void drawMiniStatValue(const Rect &r, uint16_t color, const char *text) {
  drawField(r.x + 6, r.y + 15, 16, 1, color, COL_BOX_FILL, text);
}

static void paintLiveOnce() {
  gfx->fillScreen(COL_BG);
  drawBackBtn(LIVE_BACK, false);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(LCD_W / 2 - 30, 10); gfx->print(F("LIVE VIEW"));

  gfx->fillRoundRect(LIVE_TIMER_BOX.x, LIVE_TIMER_BOX.y, LIVE_TIMER_BOX.w, LIVE_TIMER_BOX.h, 5, COL_BOX_FILL);
  gfx->drawRoundRect(LIVE_TIMER_BOX.x, LIVE_TIMER_BOX.y, LIVE_TIMER_BOX.w, LIVE_TIMER_BOX.h, 5, COL_BOX_BORDER);
  drawIconBtn(LIVE_TIMER_RST, false, 'R');

  for (uint8_t i = 0; i < 4; i++) {
    const Rect &r = LIVE_STAT[i];
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_FILL);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
    gfx->setCursor(r.x + 8, r.y + 6); gfx->print(LIVE_STAT_LABEL[i]);
  }

  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(8, 120); gfx->print(F("ENERGY"));
  drawIconBtn(LIVE_ENERGY_RST, false, 'R');
  drawMiniStatChrome(LIVE_ENERGY[0], "WH");
  drawMiniStatChrome(LIVE_ENERGY[1], "MAH");

  gfx->setCursor(8, 182); gfx->print(F("ESTIMATED ENERGY NEEDED"));
  drawMiniStatChrome(LIVE_EST[0], "2 MIN");
  drawMiniStatChrome(LIVE_EST[1], "3 MIN");

  drawActionBtn(LIVE_TARE, false, "TARE");
  drawActionBtn(LIVE_PEAK_RST, false, "PEAK RESET");

  updateLiveTick();   /* force one full value refresh under the fresh chrome */
}

static void updateLiveTick() {
  char buf[24];
  fmtCentiUnit(gState.centivolts, "V", buf, sizeof(buf));
  drawField(LIVE_STAT[0].x + 8, LIVE_STAT[0].y + 20, 9, 2, COL_VOLT, COL_BOX_FILL, buf);
  fmtCentiUnit(gState.centiamps, "A", buf, sizeof(buf));
  drawField(LIVE_STAT[1].x + 8, LIVE_STAT[1].y + 20, 9, 2, COL_AMP, COL_BOX_FILL, buf);
  fmtDegC(gState.centidegc, buf, sizeof(buf));
  drawField(LIVE_STAT[2].x + 8, LIVE_STAT[2].y + 20, 9, 2, COL_TEMP, COL_BOX_FILL, buf);
  fmtWatts(gState.milliwatts, buf, sizeof(buf));
  drawField(LIVE_STAT[3].x + 8, LIVE_STAT[3].y + 20, 9, 2, COL_TEXT, COL_BOX_FILL, buf);

  snprintf(buf, sizeof(buf), "%.2f", gState.energyWh);
  drawMiniStatValue(LIVE_ENERGY[0], COL_TEXT, buf);
  snprintf(buf, sizeof(buf), "%.0f", gState.energyMah);
  drawMiniStatValue(LIVE_ENERGY[1], COL_TEXT, buf);

  fmtMark(gState.mark2Captured, gState.mark2Wh, gState.mark2Mah, buf, sizeof(buf));
  drawMiniStatValue(LIVE_EST[0], COL_TEXT, buf);
  fmtMark(gState.mark3Captured, gState.mark3Wh, gState.mark3Mah, buf, sizeof(buf));
  drawMiniStatValue(LIVE_EST[1], COL_TEXT, buf);

  fmtTimer(gState.runElapsedMs, buf, sizeof(buf));
  drawField(LIVE_TIMER_BOX.x + 8, LIVE_TIMER_BOX.y + 11, 10, 1, COL_TEXT, COL_BOX_FILL, buf);
}

static int8_t liveHitTest(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < LIVE_BTN_N; i++)
    if (x >= LIVE_RECTS[i]->x && x < LIVE_RECTS[i]->x + LIVE_RECTS[i]->w &&
        y >= LIVE_RECTS[i]->y && y < LIVE_RECTS[i]->y + LIVE_RECTS[i]->h) return i;
  return -1;
}
static void liveSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case LIVE_BTN_BACK:       drawBackBtn(LIVE_BACK, pressed); break;
    case LIVE_BTN_TIMER_RST:  drawIconBtn(LIVE_TIMER_RST, pressed, 'R'); break;
    case LIVE_BTN_ENERGY_RST: drawIconBtn(LIVE_ENERGY_RST, pressed, 'R'); break;
    case LIVE_BTN_TARE:       drawActionBtn(LIVE_TARE, pressed, "TARE"); break;
    case LIVE_BTN_PEAK_RST:   drawActionBtn(LIVE_PEAK_RST, pressed, "PEAK RESET"); break;
  }
}
static void liveDispatch(int8_t id) {
  switch (id) {
    case LIVE_BTN_BACK: goTo(SCR_HOME); break;
    case LIVE_BTN_TIMER_RST:
    case LIVE_BTN_ENERGY_RST:
      gCmdResetEnergyTimer = true; showToast("Energy & timer reset"); break;
    case LIVE_BTN_TARE:
      gCmdTare = true; showToast("Tared - zero set"); break;
    case LIVE_BTN_PEAK_RST:
      gCmdResetPeaks = true; showToast("Peaks reset"); break;
  }
}

/* ==========================================================================
 * GRAPH — a header row of 4 present-value boxes (I/P/V/T), a scrolling
 * autoscaled plot (current always on the left axis; voltage and/or
 * temperature togglable on a shared right axis), a chip row (V/T toggles +
 * a compact run timer/reset + a hint), and a peak row pinned to the bottom
 * edge. Geometry ported from the mockup; column order/colors match between
 * the header and peak rows so the two read as aligned columns.
 * ========================================================================*/
#define PLOT_X0   38
#define PLOT_Y0   43
#define PLOT_W   380
#define PLOT_H   183
#define PLOT_X1  (PLOT_X0 + PLOT_W)
#define PLOT_Y1  (PLOT_Y0 + PLOT_H)

static const Rect GRAPH_BACK       = {   8,  6,  32, 26 };
static const Rect GRAPH_PRESENT[4] = { {46,6,102,26}, {154,6,102,26}, {262,6,102,26}, {370,6,102,26} };
static const char *GRAPH_PRESENT_LABEL[4] = { "I", "P", "V", "T" };
static const uint16_t GRAPH_PRESENT_COLOR[4] = { COL_AMP, COL_TEXT, COL_VOLT, COL_TEMP };

static const Rect GRAPH_CHIP_V     = {  86, 246, 40, 20 };
static const Rect GRAPH_CHIP_T     = { 134, 246, 40, 20 };
static const Rect GRAPH_TIMER_BOX  = { 182, 246, 66, 20 };
static const Rect GRAPH_TIMER_RST  = { 254, 246, 22, 20 };

static const Rect GRAPH_PEAK_RST   = {   8, 280,  32, 32 };
static const Rect GRAPH_PEAK[4]    = { {46,280,102,32}, {154,280,102,32}, {262,280,102,32}, {370,280,102,32} };
static const char *GRAPH_PEAK_LABEL[4] = { "PEAK I", "PEAK P", "SAG V", "PEAK T" };
static const uint16_t GRAPH_PEAK_COLOR[4] = { COL_AMP, COL_TEXT, COL_VOLT, COL_TEMP };

enum { GRAPH_BTN_BACK, GRAPH_BTN_CHIP_V, GRAPH_BTN_CHIP_T, GRAPH_BTN_TIMER_RST, GRAPH_BTN_PEAK_RST, GRAPH_BTN_N };
static const Rect *GRAPH_RECTS[GRAPH_BTN_N] = { &GRAPH_BACK, &GRAPH_CHIP_V, &GRAPH_CHIP_T, &GRAPH_TIMER_RST, &GRAPH_PEAK_RST };

static bool gShowV = true, gShowT = true;             /* persist across screen nav */
static int16_t gCurCeilingCentiamps = 1000;            /* current: 10.00 A floor    */
static int16_t gVoltBaseLo, gVoltBaseHi;               /* seeded once per entry     */
static int16_t gVoltLo, gVoltHi;
static int16_t gTempHi = 5000;                          /* 50.00 C, grows upward only */
#define TEMP_LO_CENTIDEGC 1500                          /* fixed floor, 15.00 C      */

static uint8_t gPrevY0[3][GRAPH_COLS], gPrevY1[3][GRAPH_COLS];   /* 0=I,1=V,2=T; column-diff ink */

static const int16_t I_STEPS[] = { 2, 5, 10, 25, 50, 100, 150 };   /* amps — reused from
                                                                     * display_bringup's GR_ASTEPS */

static int16_t floorTo500(int32_t v) { int32_t q = v / 500; if (v < 0 && v % 500 != 0) q--; return (int16_t)(q * 500); }
static int16_t ceilTo500(int32_t v)  { int32_t q = v / 500; if (v > 0 && v % 500 != 0) q++; return (int16_t)(q * 500); }

/* Current's ceiling recomputes fresh every frame from the visible window —
 * it can shrink back down once a spike scrolls off, same as the proven
 * grNice()/GR_ASTEPS pattern in display_bringup.ino. */
static int16_t niceCeilAmps(int16_t maxCentiamps) {
  int32_t target = (int32_t)maxCentiamps * 115 / 100;   /* 15% headroom, still centiamps */
  for (uint8_t i = 0; i < 7; i++) if ((int32_t)I_STEPS[i] * 100 >= target) return I_STEPS[i] * 100;
  return I_STEPS[6] * 100;
}

static int16_t valueToY(int16_t value, int16_t lo, int16_t hi) {
  if (hi <= lo) return PLOT_Y1 - 1;
  float f = (float)(value - lo) / (float)(hi - lo);   /* per-pixel positioning — the
    * same float divide display_bringup's own grRow() already does; this is
    * a graphics coordinate transform, not the autoscale bounds math, which
    * stays pure integer above. */
  if (f < 0) f = 0; if (f > 1) f = 1;
  int16_t y = PLOT_Y1 - 2 - (int16_t)(f * (PLOT_H - 3));
  if (y < PLOT_Y0) y = PLOT_Y0;
  if (y > PLOT_Y1 - 2) y = PLOT_Y1 - 2;
  return y;
}

static bool isGridRow(int16_t y) {
  for (uint8_t r = 0; r < 6; r++) if (PLOT_Y0 + ((int32_t)r * PLOT_H) / 5 == y) return true;
  return false;
}
static void eraseSpan(int16_t x, int16_t y0, int16_t y1) {
  if (y0 < PLOT_Y0) y0 = PLOT_Y0;
  if (y1 > PLOT_Y1 - 1) y1 = PLOT_Y1 - 1;
  for (int16_t y = y0; y <= y1; y++) gfx->drawPixel(x, y, isGridRow(y) ? COL_GRID : COL_BG);
}

static void drawGridFrame() {
  gfx->drawRect(PLOT_X0 - 1, PLOT_Y0 - 1, PLOT_W + 2, PLOT_H + 2, COL_BOX_BORDER);
  for (uint8_t r = 0; r < 6; r++) {
    int16_t y = PLOT_Y0 + ((int32_t)r * PLOT_H) / 5;
    gfx->drawFastHLine(PLOT_X0, y, PLOT_W, COL_GRID);
  }
  for (uint8_t c = 0; c < 6; c++) {
    int16_t x = PLOT_X0 + ((int32_t)c * PLOT_W) / 5;
    gfx->drawFastVLine(x, PLOT_Y0, PLOT_H, COL_GRID);
    char lbl[2]; snprintf(lbl, sizeof(lbl), "%d", 5 - c);
    gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
    gfx->setCursor(x - 2, PLOT_Y1 + 3); gfx->print(lbl);
  }
  for (uint16_t i = 0; i < GRAPH_COLS; i++) { gPrevY0[0][i] = gPrevY0[1][i] = gPrevY0[2][i] = 255; }
}

/* Bare whole-number label for a gridline value — no unit suffix, no
 * decimals. The 54px right margin has to fit two independent label columns
 * (V and T) side by side when both are shown, so these stay short on
 * purpose; the present-value and peak boxes elsewhere show full precision. */
static void fmtWhole(int16_t centi, char *buf, size_t n) {
  if (centi == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  snprintf(buf, n, "%d", centi / 100);
}

/* Redraws the axis-value labels in the plot's margins — cheap (a handful of
 * small text draws), so this just runs unconditionally every graph frame
 * rather than tracking whether the ceiling actually changed. */
static void drawAxisLabels() {
  char buf[6];
  int16_t vX = 444;                    /* right margin, V's column          */
  int16_t tX = gShowV ? 420 : 444;      /* T shares the margin when V is also shown */
  for (uint8_t r = 0; r < 6; r++) {
    int16_t y = PLOT_Y0 + ((int32_t)r * PLOT_H) / 5 - 4;
    int16_t iVal = gCurCeilingCentiamps - (int32_t)r * gCurCeilingCentiamps / 5;
    fmtWhole(iVal, buf, sizeof(buf));
    drawField(2, y, 4, 1, COL_AMP, COL_BG, buf);

    if (gShowV) {
      int16_t vVal = gVoltHi - (int32_t)r * (gVoltHi - gVoltLo) / 5;
      fmtWhole(vVal, buf, sizeof(buf));
      drawField(vX, y, 4, 1, COL_VOLT, COL_BG, buf);
    }
    if (gShowT) {
      int16_t tVal = gTempHi - (int32_t)r * (gTempHi - TEMP_LO_CENTIDEGC) / 5;
      fmtWhole(tVal, buf, sizeof(buf));
      drawField(tX, y, 4, 1, COL_TEMP, COL_BG, buf);
    }
  }
}

static void paintGraphOnce() {
  gfx->fillScreen(COL_BG);
  drawBackBtnCompact(GRAPH_BACK, false);
  for (uint8_t i = 0; i < 4; i++) {
    const Rect &r = GRAPH_PRESENT[i];
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
    gfx->setCursor(r.x + 6, r.y + 9); gfx->print(GRAPH_PRESENT_LABEL[i]);
  }

  drawGridFrame();

  gfx->setTextSize(1); gfx->setTextColor(COL_AMP);
  gfx->setCursor(8, 246); gfx->print(F("I on"));
  gfx->fillRoundRect(GRAPH_CHIP_V.x, GRAPH_CHIP_V.y, GRAPH_CHIP_V.w, GRAPH_CHIP_V.h, 4,
                      gShowV ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(GRAPH_CHIP_V.x, GRAPH_CHIP_V.y, GRAPH_CHIP_V.w, GRAPH_CHIP_V.h, 4, COL_VOLT);
  gfx->setTextColor(COL_VOLT); gfx->setCursor(GRAPH_CHIP_V.x + 6, GRAPH_CHIP_V.y + 6); gfx->print('V');
  gfx->fillRoundRect(GRAPH_CHIP_T.x, GRAPH_CHIP_T.y, GRAPH_CHIP_T.w, GRAPH_CHIP_T.h, 4,
                      gShowT ? COL_BOX_PRESSED : COL_BOX_FILL);
  gfx->drawRoundRect(GRAPH_CHIP_T.x, GRAPH_CHIP_T.y, GRAPH_CHIP_T.w, GRAPH_CHIP_T.h, 4, COL_TEMP);
  gfx->setTextColor(COL_TEMP); gfx->setCursor(GRAPH_CHIP_T.x + 6, GRAPH_CHIP_T.y + 6); gfx->print('T');

  gfx->fillRoundRect(GRAPH_TIMER_BOX.x, GRAPH_TIMER_BOX.y, GRAPH_TIMER_BOX.w, GRAPH_TIMER_BOX.h, 4, COL_BOX_FILL);
  gfx->drawRoundRect(GRAPH_TIMER_BOX.x, GRAPH_TIMER_BOX.y, GRAPH_TIMER_BOX.w, GRAPH_TIMER_BOX.h, 4, COL_BOX_BORDER);
  drawIconBtn(GRAPH_TIMER_RST, false, 'R');

  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(330, 252); gfx->print(F("V/T = right axis"));

  drawIconBtn(GRAPH_PEAK_RST, false, 'R');
  for (uint8_t i = 0; i < 4; i++) {
    const Rect &r = GRAPH_PEAK[i];
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
    gfx->setCursor(r.x + 6, r.y + 4); gfx->print(GRAPH_PEAK_LABEL[i]);
  }

  /* Voltage window seeds ONCE per graph-screen-entry: round the live
   * voltage to the nearest 5V as a center, biased toward sag (10V below,
   * 5V above) — a 12S pack won't sag to 0V, so a fixed 0-65V axis wastes
   * most of its range. Widens outward only from here (see updateGraphTick). */
  int16_t center = (int16_t)(((int32_t)gState.centivolts + 250) / 500) * 500;
  gVoltBaseLo = center - 1000; if (gVoltBaseLo < 0) gVoltBaseLo = 0;
  gVoltBaseHi = center + 500;
  gVoltLo = gVoltBaseLo; gVoltHi = gVoltBaseHi;
  gTempHi = 5000;

  updateGraphTick();   /* force one full value/plot refresh under the fresh chrome */
}

static void updateGraphTick() {
  char buf[16];
  for (uint8_t i = 0; i < 4; i++) {
    int16_t v;
    switch (i) {
      case 0: fmtCentiUnit(gState.centiamps, "A", buf, sizeof(buf)); break;
      case 1: fmtWatts(gState.milliwatts, buf, sizeof(buf)); break;
      case 2: fmtCentiUnit(gState.centivolts, "V", buf, sizeof(buf)); break;
      case 3: fmtDegC(gState.centidegc, buf, sizeof(buf)); break;
    }
    drawField(GRAPH_PRESENT[i].x + 20, GRAPH_PRESENT[i].y + 9, 12, 1, GRAPH_PRESENT_COLOR[i], COL_BOX_FILL, buf);
  }

  fmtCentiUnit(gState.peakCentiamps, "A", buf, sizeof(buf));
  drawMiniStatValue(GRAPH_PEAK[0], COL_AMP, buf);
  fmtWatts(gState.peakMilliwatts, buf, sizeof(buf));
  drawMiniStatValue(GRAPH_PEAK[1], COL_TEXT, buf);
  fmtCentiUnit(gState.peakVAtPeakCentivolts, "V", buf, sizeof(buf));
  drawMiniStatValue(GRAPH_PEAK[2], COL_VOLT, buf);
  fmtDegC(gState.peakCentidegc, buf, sizeof(buf));
  drawMiniStatValue(GRAPH_PEAK[3], COL_TEMP, buf);

  fmtTimer(gState.runElapsedMs, buf, sizeof(buf));
  drawField(GRAPH_TIMER_BOX.x + 4, GRAPH_TIMER_BOX.y + 6, 10, 1, COL_TEXT, COL_BOX_FILL, buf);

  /* ---- autoscale: pure integer min/max scan + round-to-nearest-500 ---- */
  int16_t maxI = 0, minV = 32767, maxV = -32768, maxT = TEMP_LO_CENTIDEGC;
  for (uint16_t c = 0; c < GRAPH_COLS; c++) {
    if (gRingI[c] > maxI) maxI = gRingI[c];
    if (gRingV[c] < minV) minV = gRingV[c];
    if (gRingV[c] > maxV) maxV = gRingV[c];
    if (gRingT[c] > maxT) maxT = gRingT[c];
  }
  gCurCeilingCentiamps = niceCeilAmps(maxI);

  int16_t candLo = floorTo500((int32_t)minV - 200);
  int16_t candHi = ceilTo500((int32_t)maxV + 200);
  gVoltLo = (candLo < gVoltBaseLo) ? candLo : gVoltBaseLo;
  gVoltHi = (candHi > gVoltBaseHi) ? candHi : gVoltBaseHi;

  int16_t candTHi = ceilTo500((int32_t)maxT + 300);
  gTempHi = (candTHi > 5000) ? candTHi : 5000;

  drawAxisLabels();

  /* ---- plot: column-diff ink, "connect the dots" via a vertical span
   * between this column's value and the previous column's — the same
   * technique display_bringup's grDraw() already validated. Channel bounds
   * are fixed for the whole frame, so this table is built once, not per
   * column. */
  struct { const int16_t *ring; int16_t lo, hi; uint16_t color; bool show; } chans[3] = {
    { gRingI, 0, gCurCeilingCentiamps, COL_AMP, true },
    { gRingV, gVoltLo, gVoltHi, COL_VOLT, gShowV },
    { gRingT, TEMP_LO_CENTIDEGC, gTempHi, COL_TEMP, gShowT },
  };
  for (uint16_t c = 0; c < GRAPH_COLS; c++) {
    uint16_t idx = (gRingHead + 1 + c) % GRAPH_COLS;
    uint16_t prev = (idx + GRAPH_COLS - 1) % GRAPH_COLS;
    int16_t x = PLOT_X0 + c;

    for (uint8_t ch = 0; ch < 3; ch++) {
      if (!chans[ch].show) continue;
      int16_t yNow  = valueToY(chans[ch].ring[idx],  chans[ch].lo, chans[ch].hi);
      int16_t yPrev = (c == 0) ? yNow : valueToY(chans[ch].ring[prev], chans[ch].lo, chans[ch].hi);
      int16_t y0 = (yNow < yPrev) ? yNow : yPrev;
      int16_t y1 = ((yNow > yPrev) ? yNow : yPrev) + 1;
      if (gPrevY0[ch][c] != 255 && (gPrevY0[ch][c] != y0 || gPrevY1[ch][c] != y1))
        eraseSpan(x, gPrevY0[ch][c], gPrevY1[ch][c]);
      gfx->drawFastVLine(x, y0, y1 - y0 + 1, chans[ch].color);
      gPrevY0[ch][c] = (uint8_t)y0; gPrevY1[ch][c] = (uint8_t)y1;
    }
  }
}

static int8_t graphHitTest(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < GRAPH_BTN_N; i++)
    if (x >= GRAPH_RECTS[i]->x && x < GRAPH_RECTS[i]->x + GRAPH_RECTS[i]->w &&
        y >= GRAPH_RECTS[i]->y && y < GRAPH_RECTS[i]->y + GRAPH_RECTS[i]->h) return i;
  return -1;
}
static void graphSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case GRAPH_BTN_BACK:      drawBackBtn(GRAPH_BACK, pressed); break;
    case GRAPH_BTN_TIMER_RST: drawIconBtn(GRAPH_TIMER_RST, pressed, 'R'); break;
    case GRAPH_BTN_PEAK_RST:  drawIconBtn(GRAPH_PEAK_RST, pressed, 'R'); break;
    default: break;   /* the V/T chips redraw via their own toggled state, not a press flash */
  }
}
static void graphDispatch(int8_t id) {
  switch (id) {
    case GRAPH_BTN_BACK: goTo(SCR_HOME); break;
    case GRAPH_BTN_CHIP_V:
      gShowV = !gShowV;
      gfx->fillRoundRect(GRAPH_CHIP_V.x, GRAPH_CHIP_V.y, GRAPH_CHIP_V.w, GRAPH_CHIP_V.h, 4,
                          gShowV ? COL_BOX_PRESSED : COL_BOX_FILL);
      gfx->drawRoundRect(GRAPH_CHIP_V.x, GRAPH_CHIP_V.y, GRAPH_CHIP_V.w, GRAPH_CHIP_V.h, 4, COL_VOLT);
      gfx->setTextSize(1); gfx->setTextColor(COL_VOLT);
      gfx->setCursor(GRAPH_CHIP_V.x + 6, GRAPH_CHIP_V.y + 6); gfx->print('V');
      break;
    case GRAPH_BTN_CHIP_T:
      gShowT = !gShowT;
      gfx->fillRoundRect(GRAPH_CHIP_T.x, GRAPH_CHIP_T.y, GRAPH_CHIP_T.w, GRAPH_CHIP_T.h, 4,
                          gShowT ? COL_BOX_PRESSED : COL_BOX_FILL);
      gfx->drawRoundRect(GRAPH_CHIP_T.x, GRAPH_CHIP_T.y, GRAPH_CHIP_T.w, GRAPH_CHIP_T.h, 4, COL_TEMP);
      gfx->setTextSize(1); gfx->setTextColor(COL_TEMP);
      gfx->setCursor(GRAPH_CHIP_T.x + 6, GRAPH_CHIP_T.y + 6); gfx->print('T');
      break;
    case GRAPH_BTN_TIMER_RST:
      gCmdResetEnergyTimer = true; showToast("Energy & timer reset"); break;
    case GRAPH_BTN_PEAK_RST:
      gCmdResetPeaks = true; showToast("Peaks reset"); break;
  }
}

/* ==========================================================================
 * CALIBRATE — read-only. Shows the baked constants above and their
 * compile-time status (FITTED/MEASURED/NOMINAL/ANCHORED). Recalibrating
 * still means running display_bringup.ino's serial v/i/n commands and
 * pasting fresh numbers into the #defines at the top of this file, then
 * reflashing — this sketch doesn't duplicate that interactive serial menu
 * (see the SCOPE note in the file header). Values here never change at
 * runtime, so unlike every other screen, nothing needs a per-tick update.
 * ========================================================================*/
static const Rect CAL_BACK = { 8, 8, 52, 30 };

struct CalRow { const char *name; char val[16]; const char *tag; };
static CalRow gCalRows[7];

static void buildCalRows() {
  snprintf(gCalRows[0].val, sizeof(gCalRows[0].val), "%.8f", V_GAIN_CAL);
  snprintf(gCalRows[1].val, sizeof(gCalRows[1].val), "%.5f", V_OFFSET_CAL);
  snprintf(gCalRows[2].val, sizeof(gCalRows[2].val), "%.6f", I_QUIESCENT_CAL);
  snprintf(gCalRows[3].val, sizeof(gCalRows[3].val), "%.8f", I_SENS_CAL);
  snprintf(gCalRows[4].val, sizeof(gCalRows[4].val), "%.1f", RV1_OHMS);
  snprintf(gCalRows[5].val, sizeof(gCalRows[5].val), "%.1f", NTC_B);
  snprintf(gCalRows[6].val, sizeof(gCalRows[6].val), "%.1f", NTC_R25);
  gCalRows[0].name = "V_GAIN_CAL";     gCalRows[0].tag = V_CAL_VALID   ? "FITTED"   : "NOMINAL";
  gCalRows[1].name = "V_OFFSET_CAL";   gCalRows[1].tag = V_CAL_VALID   ? "FITTED"   : "NOMINAL";
  gCalRows[2].name = "I_QUIESCENT_CAL"; gCalRows[2].tag = I_ZERO_VALID ? "MEASURED" : "NOMINAL";
  gCalRows[3].name = "I_SENS_CAL";     gCalRows[3].tag = I_GAIN_VALID  ? "FITTED"   : "NOMINAL";
  gCalRows[4].name = "RV1_OHMS";       gCalRows[4].tag = "MEASURED";
  gCalRows[5].name = "NTC_B";          gCalRows[5].tag = "FITTED";
  gCalRows[6].name = "NTC_R25";        gCalRows[6].tag = "ANCHORED";
}

static void paintCalOnce() {
  gfx->fillScreen(COL_BG);
  drawBackBtn(CAL_BACK, false);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(LCD_W / 2 - 30, 10); gfx->print(F("CALIBRATE"));

  buildCalRows();
  for (uint8_t i = 0; i < 7; i++) {
    int16_t y = 44 + i * 32;
    gfx->drawFastHLine(18, y + 30, 444, COL_BOX_BORDER);
    gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
    gfx->setCursor(18, y + 4); gfx->print(gCalRows[i].name);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(18, y + 16); gfx->print(gCalRows[i].val);
    gfx->setTextColor(COL_AMP);
    gfx->drawRect(300, y + 14, 60, 12, COL_AMP);
    gfx->setCursor(304, y + 16); gfx->print(gCalRows[i].tag);
  }

  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(20, 286);
  gfx->print(F("Read-only. Recalibrate via display_bringup's"));
  gfx->setCursor(20, 298);
  gfx->print(F("serial v/i/n, then paste values in + reflash."));
}

static int8_t calHitTest(int16_t x, int16_t y) {
  const Rect &r = CAL_BACK;
  return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) ? 0 : -1;
}
static void calSetPressed(int8_t id, bool pressed) { (void)id; drawBackBtn(CAL_BACK, pressed); }
static void calDispatch(int8_t id) { (void)id; goTo(SCR_HOME); }

/* ==========================================================================
 * Screen-agnostic touch dispatch — routes to each screen's own hit-test/
 * dispatch/pressed-visual functions above. This is the only place that
 * needs to know all four screens exist.
 * ========================================================================*/
static int8_t hitTestScreen(ScreenId s, int16_t x, int16_t y) {
  switch (s) {
    case SCR_HOME:  return homeHitTest(x, y);
    case SCR_LIVE:  return liveHitTest(x, y);
    case SCR_GRAPH: return graphHitTest(x, y);
    case SCR_CAL:   return calHitTest(x, y);
  }
  return -1;
}
static void dispatch(ScreenId s, int8_t id) {
  switch (s) {
    case SCR_HOME:  homeDispatch(id);  break;
    case SCR_LIVE:  liveDispatch(id);  break;
    case SCR_GRAPH: graphDispatch(id); break;
    case SCR_CAL:   calDispatch(id);   break;
  }
}
static void setPressedVisual(ScreenId s, int8_t id, bool pressed) {
  switch (s) {
    case SCR_HOME:  homeSetPressed(id, pressed);  break;
    case SCR_LIVE:  liveSetPressed(id, pressed);  break;
    case SCR_GRAPH: graphSetPressed(id, pressed); break;
    case SCR_CAL:   calSetPressed(id, pressed);   break;
  }
}
