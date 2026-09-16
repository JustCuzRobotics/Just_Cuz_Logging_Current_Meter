/* ============================================================================
 * TOUCH DEV TEST — standalone touchscreen + display diagnostic
 * ----------------------------------------------------------------------------
 *   VERSION 1.3          LAST UPDATED 2026-09-08 08:51 EDT
 *
 *   1.3  2026-09-08  v1.2's read-rate floor changed NOTHING -- holds still
 *                    show an instant UP every single time, no exceptions.
 *                    That rules out polling cadence/mode as the cause: if
 *                    rate-limiting the reads and widening the debounce
 *                    tolerance doesn't even partially help, the raw
 *                    touchRead() must be genuinely failing (or reporting
 *                    zero touches) almost immediately after the first good
 *                    sample of a hold, independent of how often it's asked.
 *                    Problem is touchRead() couldn't say WHY it failed --
 *                    its own old comment admits it collapses "I2C transport
 *                    error" and "chip cleanly says zero touches" into one
 *                    bool, an ambiguity inherited from display_bringup.ino
 *                    and never resolved. Split it into RC_OK/RC_I2C_ERR/
 *                    RC_NO_TOUCH/RC_BAD_N (see the comment by the #defines
 *                    and by touchRead() itself). Every absorbed miss during
 *                    a hold now tallies into gErrCount/gZeroCount/
 *                    gBadNCount (shown on-screen as ERR/Z/N, replacing the
 *                    single GLITCH field) and every UP's Serial line prints
 *                    that specific hold's breakdown. A high ERR count means
 *                    the I2C bus/wiring/pull-ups/crosstalk layer is at
 *                    fault; a high Z count means the FT6336U's own touch-
 *                    reporting logic is dropping a real, continuing contact
 *                    -- two different problems with two different fixes,
 *                    and this is the next hardware run's job to tell apart.
 *   1.2  2026-09-08  Bench data from v1.1 (polling mode) made it worse, not
 *                    better: EVERY hold now showed an instant UP (never once
 *                    registered a genuine hold or drag across a full test
 *                    run), and the glitch/touch ratio (69/22) sat right at
 *                    TOUCH_MISS_TOLERANCE's 3-miss ceiling on every single
 *                    touch -- too consistent to be random noise. Root cause
 *                    found in pollTouch()'s own gating: the IRQ-triggered
 *                    read path had NO minimum spacing at all -- only the
 *                    20ms timer fallback was rate-limited. In polling mode
 *                    CTP_INT pulses every internal scan cycle regardless of
 *                    touch state (not just on state changes, unlike trigger
 *                    mode), so gTouchIrq was getting serviced on nearly every
 *                    loop() iteration -- thousands of reads/sec instead of
 *                    the ~50/sec (20ms) the TOUCH_MISS_TOLERANCE=3 grace
 *                    window ("~60ms") was designed around. 3 misses at that
 *                    rate burns the whole grace window in well under a
 *                    millisecond, so any hold reads as an instant release --
 *                    matches the symptom exactly. Fix: added TOUCH_MIN_READ_MS
 *                    (8ms) as a floor on ANY touchRead(), IRQ-triggered
 *                    included, and scaled TOUCH_MISS_TOLERANCE 3->8 to keep
 *                    the same ~60ms real-world grace at the new, faster,
 *                    but now-BOUNDED sample rate. Left the controller in
 *                    polling mode (0xA4=0x00) from v1.1 to isolate this fix
 *                    as its own variable -- if holds/drags still don't
 *                    register after this, try reverting to trigger mode
 *                    (0xA4=0x01) next, now that the read-storm bug can't
 *                    confound the result either way.
 *   1.1  2026-09-08  Switched the FT6336U from TRIGGER mode to POLLING mode
 *                    (0xA4: 0x01 -> 0x00). Bench data from v1.0 showed a real
 *                    held touch producing wildly inconsistent raw coordinates
 *                    mid-hold (one DRAG event jumped 326px with no plausible
 *                    finger movement) and drags/holds failing to register
 *                    most of the time -- both point at the controller
 *                    returning stale/garbage report data between genuine
 *                    state-change events, which is exactly the failure mode
 *                    trigger mode invites: it only refreshes what CTP_INT
 *                    reports on a change, and this sketch's 20ms poll
 *                    fallback (TOUCH_POLL_MS) can land between those
 *                    refreshes. Polling mode keeps the controller's report
 *                    registers live on every internal scan regardless of
 *                    interrupt servicing, so a timer-driven poll always
 *                    reads current data. This was on the "not yet attempted"
 *                    list carried over from logging_current_meter_ui.ino's
 *                    v1.7 handoff notes. gIrqCount/gPollCount on-screen still
 *                    show whether CTP_INT is firing at all under this mode --
 *                    watch STATS while doing a real hold to confirm this
 *                    actually fixes it before porting the change back to
 *                    logging_current_meter_ui.ino.
 *   1.0  2026-09-08  First cut. Small on-screen touch targets have stayed
 *                    unreliable across several rounds of fixes already
 *                    tried in logging_current_meter_ui.ino (interrupt-based
 *                    touch, TOUCH_HIT_SLOP hit-region expansion, hold
 *                    debounce), and swapping to a second physical display
 *                    made no difference -- which rules out a bad panel/
 *                    touch-chip unit but not a wiring/config/architecture
 *                    cause. This is a from-scratch, display+touch-ONLY
 *                    sketch: no dual-core ADC sampling, no calibration
 *                    math, no multi-screen nav, none of the full UI's
 *                    per-tick drawing load -- just enough to hammer on
 *                    touch targets and watch exactly what the controller
 *                    reports, to isolate whether the problem is inherent
 *                    to the FT6336U/wiring or an artifact of the full
 *                    app's architecture/timing.
 *
 *   Reuses the display bring-up and FT6336U driver code verbatim from
 *   display_bringup.ino / logging_current_meter_ui.ino (pin map, GFX bus
 *   construction, touch register init sequence -- including the
 *   monitor-mode-forbidden write that was the main fix back in bring-up,
 *   and touchRead()/touchMap()) -- all hardware-proven, not re-derived
 *   here. Everything else (test buttons, stats, crosshair) is new.
 *
 *   Hardware: Waveshare RP2040-Zero on the Logging_Current_Meter Rev A PCB.
 *   Display: lcdwiki MSP3526 / Hosyond 3.5" 480x320 IPS, ST7796U + FT6336U
 *   capacitive touch, on a shared 14-way 0.5 mm FPC.
 *
 *   Toolchain: board "Waveshare RP2040 Zero" (arduino-pico core, Earle
 *   Philhower). Library "GFX Library for Arduino" by moononournation
 *   (Arduino_GFX — NOT Adafruit_GFX, different header).
 *
 *   HOW TO USE THIS: try to reproduce the "hard to hit" feeling against the
 *   small test targets -- TL/TR/BL/BR (30x30 corners), C1/C2/C3 (32x24,
 *   8px gaps -- deliberately the same size/spacing as the Graph screen's
 *   V/T chips in the main UI), and 1/2/3/4 (20x20, 8px gaps, an even
 *   tighter worst case). MEDIUM and LARGE are easy-target controls for
 *   comparison. RESET zeroes every counter for a clean run.
 *
 *   Reading the stats line while you miss a tap:
 *     - RAW/MAP never updates at all  -> touchRead() isn't seeing the
 *       touch. Controller/wiring/timing problem, not a UI problem.
 *     - RAW/MAP updates but lands outside every button box -> a real
 *       coordinate-mapping or geometry/slop problem, not a controller
 *       problem.
 *     - ERR/Z/N (v1.3) classify raw touchRead() dropouts absorbed during a
 *       still-held tap, instead of one lumped GLITCH count: ERR is an I2C
 *       transport failure (NAK/timeout -- bus/wiring/electrical layer); Z
 *       is a clean I2C read where the FT6336U itself reports zero touches
 *       (a controller-firmware/report-behavior problem); N is a clean read
 *       reporting a garbage touch count (noise). High ERR points at the
 *       hardware link; high Z points at the chip's own touch-reporting
 *       logic, not the wiring.
 *     - IRQ vs POLL shows whether CTP_INT is actually firing or whether
 *       every read is falling back to the 20ms timer.
 *   Serial (115200, Newline line ending) logs every DOWN/UP/DRAG event with
 *   raw+mapped coordinates, and every UP also logs that hold's ERR/Z/N
 *   breakdown (miss[err=..,zero=..,badN=..]) -- all zero means the release
 *   was real, not a debounce timeout.
 * ==========================================================================*/

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * Pin map — identical to display_bringup.ino / logging_current_meter_ui.ino
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

#define TOUCH_POLL_MS          20   /* fallback timer, same cadence as the main UI */
#define TOUCH_MIN_READ_MS       8   /* floor on ANY touchRead(), IRQ-triggered included --
                                     * v1.2. Faster than the FT6336U's own configured
                                     * PERIODACTIVE scan (0x88=0x04, ~4ms) so we don't
                                     * needlessly double-sample the same internal scan,
                                     * slower than "every loop() tick" so an IRQ storm
                                     * (CTP_INT pulsing continuously in polling mode, or
                                     * bouncing) can't call touchRead() thousands of times
                                     * a second. */
#define TOUCH_MISS_TOLERANCE    8   /* v1.2: was 3. At the old TOUCH_POLL_MS=20ms-only
                                     * cadence 3 misses gave ~60ms of hold-debounce grace;
                                     * now that IRQ-triggered reads can happen as fast as
                                     * TOUCH_MIN_READ_MS, the same 3-miss ceiling could
                                     * burn through in single-digit milliseconds instead --
                                     * see the v1.2 changelog entry above. 8 * 8ms keeps
                                     * the ~60ms grace window roughly intact at the new,
                                     * faster, BOUNDED sample rate. */
#define DRAG_THRESHOLD_PX        6  /* movement from press-start before calling it a drag */

/* Hit-test tolerance — identical constant, identical purpose, to the main
 * UI's TOUCH_HIT_SLOP. The C1/C2/C3 chip row and the 1/2/3/4 cluster below
 * are sized/gapped to deliberately reproduce the main UI's tightest real
 * targets (Graph screen's V/T chips: 8px gaps = TOUCH_HIT_SLOP*2). */
#define TOUCH_HIT_SLOP    4

#define FT_ADDR           0x38
#define FT_REG_TD_STATUS  0x02

/* ==========================================================================
 * Palette — flat fills only, no gradients (subset of the main UI's palette)
 * ========================================================================*/
#define COL_BG              0x0041   /* #070a0e */
#define COL_GRID             0x2209   /* #22404d */
#define COL_TEXT             0xDF3D   /* #dce7ec — primary near-white text   */
#define COL_TEXT_HI          0xEF7E   /* #e8eef1 — labels, near-white        */
#define COL_AMP               0xFD84   /* #ffb020 — crosshair (idle)          */
#define COL_DANGER            0xFB4B   /* #ff6b5e — crosshair (dragging), RESET border */
#define COL_BOX_FILL          0x10E4   /* #131c22 */
#define COL_BOX_BORDER        0x2209   /* #25404c */
#define COL_BOX_PRESSED       0x1987   /* #1c3038 */
#define COL_DISABLED_FILL     0x08A3   /* #0f1418 — RESET's idle fill        */

/* ==========================================================================
 * GFX / touch objects — construction identical to display_bringup.ino
 * ========================================================================*/
Arduino_DataBus *bus = new Arduino_RPiPicoSPI(
    PIN_LCD_RS, PIN_LCD_CS, PIN_SCK, PIN_MOSI, PIN_MISO, spi0);
Arduino_GFX *gfx = new Arduino_ST7796(
    bus, PIN_LCD_RST, LCD_ROTATION, true /* IPS */);

/* ==========================================================================
 * Small shared geometry type + forward declarations
 * ==========================================================================
 * Arduino's auto-prototype generator inserts its own prototype block near
 * the TOP of the file, before struct Rect is declared -- so any function
 * taking a Rect parameter needs a manual forward declaration somewhere in
 * the file, or the auto-generated prototype fails to compile referencing an
 * undeclared type. (Same hazard, same fix, as logging_current_meter_ui.ino.)
 * ========================================================================*/
struct Rect { int16_t x, y, w, h; };
static void centerText(const Rect &r, const char *text, uint8_t size, uint16_t color, int8_t yOffset);

/* ==========================================================================
 * Test button layout
 * ----------------------------------------------------------------------------
 * All eleven targets fit 4..476 x 64..310 below the status block, with a
 * verified 12px gap (>= TOUCH_HIT_SLOP*2 + 4px margin) between every
 * adjacent row so the hit regions below never bleed into each other:
 *   TL/TR (30x30, corners)      y  64.. 94
 *   C1/C2/C3 (32x24, gap 8)     y 106..130
 *   1/2/3/4 (20x20, gap 8)      y 142..162
 *   MEDIUM (140x36)             y 174..210
 *   LARGE (200x46)              y 222..268
 *   BL/BR/RESET (30x30 / 120x30) y 280..310
 * ========================================================================*/
enum {
  BTN_TL, BTN_TR,
  BTN_C1, BTN_C2, BTN_C3,
  BTN_T1, BTN_T2, BTN_T3, BTN_T4,
  BTN_MEDIUM, BTN_LARGE,
  BTN_BL, BTN_BR, BTN_RESET,
  BTN_N
};

static const Rect BTN_RECTS[BTN_N] = {
  {   4,  64,  30, 30 },   /* TL */
  { 446,  64,  30, 30 },   /* TR */
  { 184, 106,  32, 24 },   /* C1 */
  { 224, 106,  32, 24 },   /* C2 */
  { 264, 106,  32, 24 },   /* C3 */
  { 188, 142,  20, 20 },   /* 1 */
  { 216, 142,  20, 20 },   /* 2 */
  { 244, 142,  20, 20 },   /* 3 */
  { 272, 142,  20, 20 },   /* 4 */
  { 170, 174, 140, 36 },   /* MEDIUM */
  { 140, 222, 200, 46 },   /* LARGE */
  {   4, 280,  30, 30 },   /* BL */
  { 446, 280,  30, 30 },   /* BR */
  { 180, 280, 120, 30 },   /* RESET */
};
static const char *BTN_LABELS[BTN_N] = {
  "TL", "TR", "C1", "C2", "C3", "1", "2", "3", "4",
  "MEDIUM", "LARGE", "BL", "BR", "RESET"
};
static uint16_t btnHitCount[BTN_N] = { 0 };
static bool     gBtnPressed[BTN_N] = { false };

#define STATUS_DIVIDER_Y  61

/* ==========================================================================
 * FT6336U capacitive touch — I2C1 (Wire1), reused verbatim from
 * display_bringup.ino / logging_current_meter_ui.ino: same reset pulse,
 * same register writes (monitor mode FORBIDDEN is the one that matters —
 * the factory default allows it and it silently drops the scan rate after
 * 30 s idle), same 100kHz->400kHz switch.
 * ========================================================================*/
static bool ftReadBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)FT_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}

/* touchRead() result codes -- v1.3. Splits the "false" that used to mean
 * BOTH "I2C transport failed" and "chip cleanly reported zero touches"
 * (see touchRead()'s old comment, inherited unexamined from
 * display_bringup.ino) into three distinguishable outcomes, so a hold's
 * miss streak can be classified instead of just counted. RC_I2C_ERR points
 * at the bus/wiring/electrical layer (NAK, timeout, bad read length);
 * RC_NO_TOUCH means the transaction succeeded fine and the FT6336U itself
 * insists nothing is touching; RC_BAD_N is a garbage/noise touch count. */
#define RC_OK        0
#define RC_I2C_ERR   1
#define RC_NO_TOUCH  2
#define RC_BAD_N     3
static void ftWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg); Wire1.write(val);
  Wire1.endTransmission();
}

/* Real hardware interrupt for touch, matching display_bringup.ino's proven
 * dual mechanism: a bare ISR that only sets a flag (never touches I2C from
 * interrupt context), ORed in pollTouch() with a timer fallback. gIrqCount/
 * gPollCount below track which path actually triggered each read, so you
 * can see on-screen whether CTP_INT is doing its job. */
volatile bool gTouchIrq = false;
static void ctpIsr() { gTouchIrq = true; }

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
  ftWrite(0xA4, 0x00);   /* POLLING mode (was 0x01/trigger) --  *
                          * keeps report regs live every scan,  *
                          * v1.1                                 */
  Wire1.setClock(400000);
  delay(5);

  attachInterrupt(digitalPinToInterrupt(PIN_CTP_INT), ctpIsr, FALLING);
}

/* v1.3: returns one of RC_OK/RC_I2C_ERR/RC_NO_TOUCH/RC_BAD_N instead of a
 * bare bool -- see the RC_* comment above. This is the one deliberate
 * departure from "reused verbatim" in this sketch: display_bringup.ino and
 * logging_current_meter_ui.ino both still use the old ambiguous bool form,
 * so this classification needs porting back there too once it's told us
 * something useful. */
static int8_t touchRead(uint16_t *rx, uint16_t *ry) {
  uint8_t b[5];
  if (!ftReadBlock(FT_REG_TD_STATUS, b, 5)) return RC_I2C_ERR;
  uint8_t n = b[0] & 0x0F;
  if (n == 0) return RC_NO_TOUCH;
  if (n > 2) return RC_BAD_N;
  *rx = (uint16_t)(b[1] & 0x0F) << 8 | b[2];
  *ry = (uint16_t)(b[3] & 0x0F) << 8 | b[4];
  return RC_OK;
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
 * Drawing helpers
 * ========================================================================*/

/* Erases a FIXED-size field (sized for maxChars at the given text size) then
 * draws newText — same convention as logging_current_meter_ui.ino's
 * drawField(). Optional `cache` (caller-owned, size >= maxChars+1) skips the
 * erase+redraw entirely when text hasn't changed since last call. */
static bool drawField(int16_t x, int16_t y, uint8_t maxChars, uint8_t textSize,
                       uint16_t fg, uint16_t bg, const char *text,
                       char *cache = nullptr) {
  if (cache && strncmp(cache, text, maxChars) == 0) return false;
  if (cache) { strncpy(cache, text, maxChars); cache[maxChars] = 0; }
  gfx->fillRect(x, y, maxChars * 6 * textSize, 8 * textSize, bg);
  gfx->setTextSize(textSize);
  gfx->setTextColor(fg);
  gfx->setCursor(x, y);
  gfx->print(text);
  return true;
}

static void centerText(const Rect &r, const char *text, uint8_t size, uint16_t color, int8_t yOffset) {
  uint8_t n = (uint8_t)strlen(text);
  int16_t tw = (int16_t)(n * 6 * size);
  int16_t th = (int16_t)(8 * size);
  int16_t x = r.x + (r.w - tw) / 2;
  int16_t y = r.y + (r.h - th) / 2 + yOffset;
  if (x < r.x) x = r.x;
  if (y < r.y) y = r.y;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(text);
}

/* Draws one test button's fill+border+label(+hit count, on the two big
 * ones). Called both from paintOnce() (idle state) and from pollTouch() on
 * every press/release edge (pressed/idle state + updated count). */
static void drawButton(uint8_t id, bool pressed) {
  const Rect &r = BTN_RECTS[id];
  uint16_t fill = pressed ? COL_BOX_PRESSED
                : (id == BTN_RESET ? COL_DISABLED_FILL : COL_BOX_FILL);
  uint16_t border = (id == BTN_RESET) ? COL_DANGER : COL_BOX_BORDER;
  gfx->fillRect(r.x, r.y, r.w, r.h, fill);
  gfx->drawRect(r.x, r.y, r.w, r.h, border);

  bool bigLabel  = (r.w >= 90);
  bool hasCount  = (r.h >= 32) && (id != BTN_RESET);
  uint8_t labelSize = bigLabel ? 2 : 1;

  if (hasCount) {
    Rect top = { r.x, r.y, r.w, (int16_t)(r.h - 10) };
    centerText(top, BTN_LABELS[id], labelSize, COL_TEXT_HI, 0);
    char cbuf[8];
    snprintf(cbuf, sizeof(cbuf), "#%u", btnHitCount[id]);
    Rect bot = { r.x, (int16_t)(r.y + r.h - 10), r.w, 10 };
    centerText(bot, cbuf, 1, COL_TEXT, 0);
  } else {
    centerText(r, BTN_LABELS[id], labelSize, COL_TEXT_HI, 0);
  }
}

/* Restores a rectangular screen area to idle chrome: background fill, the
 * status divider if it intersects, and any test button that intersects
 * (redrawn at its currently-tracked pressed state, not forced idle — a
 * button being actively held must not flicker unpressed just because the
 * crosshair's erase pass swept over it). Used only to erase the crosshair's
 * old position, so the intersecting-button check only ever touches a
 * handful of the 14 rects — cheap. */
static void restoreArea(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  gfx->fillRect(x, y, w, h, COL_BG);
  if (y <= STATUS_DIVIDER_Y && y + h > STATUS_DIVIDER_Y) {
    gfx->drawFastHLine(0, STATUS_DIVIDER_Y, LCD_W, COL_GRID);
  }
  for (uint8_t i = 0; i < BTN_N; i++) {
    const Rect &r = BTN_RECTS[i];
    if (x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y) {
      drawButton(i, gBtnPressed[i]);
    }
  }
}

/* Crosshair: erases its own previous position (via restoreArea, which
 * correctly redraws any button underneath) before drawing at the new one.
 * Color flags whether a drag is in progress. Pass x<0 to erase-only (no
 * touch currently down). */
static void updateCrosshair(int16_t x, int16_t y, bool dragging) {
  static int16_t lastCx = -100, lastCy = -100;
  static bool wasShown = false;
  const int8_t R = 8;

  if (wasShown) {
    restoreArea((int16_t)(lastCx - R - 1), (int16_t)(lastCy - R - 1),
                (int16_t)(2 * (R + 1) + 1), (int16_t)(2 * (R + 1) + 1));
    wasShown = false;
  }
  if (x >= 0 && y >= 0) {
    uint16_t col = dragging ? COL_DANGER : COL_AMP;
    gfx->drawFastHLine((int16_t)(x - R), y, (int16_t)(2 * R + 1), col);
    gfx->drawFastVLine(x, (int16_t)(y - R), (int16_t)(2 * R + 1), col);
    gfx->drawCircle(x, y, 4, col);
    lastCx = x; lastCy = y; wasShown = true;
  }
}

static int8_t hitTestBtn(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < BTN_N; i++) {
    const Rect &r = BTN_RECTS[i];
    if (x >= r.x - TOUCH_HIT_SLOP && x < r.x + r.w + TOUCH_HIT_SLOP &&
        y >= r.y - TOUCH_HIT_SLOP && y < r.y + r.h + TOUCH_HIT_SLOP) return (int8_t)i;
  }
  return -1;
}

/* ==========================================================================
 * Stats
 * ========================================================================*/
uint32_t gTouchCount  = 0;   /* debounced press-down edges                  */
uint32_t gErrCount    = 0;   /* v1.3: RC_I2C_ERR misses absorbed by debounce -- bus/wiring layer   */
uint32_t gZeroCount   = 0;   /* v1.3: RC_NO_TOUCH misses absorbed by debounce -- chip says "no touch" */
uint32_t gBadNCount   = 0;   /* v1.3: RC_BAD_N misses absorbed by debounce -- garbage touch count   */
uint32_t gIrqCount    = 0;   /* reads triggered by CTP_INT                  */
uint32_t gPollCount   = 0;   /* reads triggered by the 20ms fallback timer  */
uint32_t gFps         = 0;
char     gLastHitLabel[10] = "-";
uint32_t gLastHitCount = 0;

bool    gDown = false, gDragging = false;
int16_t gPressStartX = 0, gPressStartY = 0;
int16_t gCurX = 0, gCurY = 0, gDragDist = 0;
uint16_t gRawX = 0, gRawY = 0;

static void resetStats() {
  gTouchCount = gErrCount = gZeroCount = gBadNCount = gIrqCount = gPollCount = 0;
  for (uint8_t i = 0; i < BTN_N; i++) btnHitCount[i] = 0;
  strncpy(gLastHitLabel, "-", sizeof(gLastHitLabel));
  gLastHitCount = 0;
  Serial.println(F("=== STATS RESET ==="));
}

/* ==========================================================================
 * Touch polling — mirrors logging_current_meter_ui.ino v1.7's pollTouch()
 * (interrupt OR 20ms-timer trigger, same TOUCH_MISS_TOLERANCE hold-debounce)
 * so results here transfer directly to the main UI, plus the extra
 * instrumentation (glitch/irq/poll counts, drag tracking, per-button hit
 * counts, Serial event log) that's the whole point of this sketch.
 * ========================================================================*/
static void pollTouch() {
  static uint32_t lastPollMs = 0;
  static uint8_t  missCount = 0;
  static uint16_t lastRx = 0, lastRy = 0;
  static int16_t  lastSx = 0, lastSy = 0;
  static int8_t   pressedBtn = -1;
  static uint16_t holdErr = 0, holdZero = 0, holdBadN = 0;  /* v1.3: per-hold breakdown */

  uint32_t now = millis();
  uint32_t sinceLast = now - lastPollMs;
  bool dueToIrq = gTouchIrq;

  /* v1.2: floor on ANY read, IRQ included -- see TOUCH_MIN_READ_MS above.
   * Clears a pending IRQ flag so it doesn't just re-fire the instant this
   * floor clears (which would otherwise reduce to the same busy-loop this
   * is meant to prevent), but skips the actual I2C transaction. Not
   * counted as either an IRQ or a poll read since no read happened. */
  if (sinceLast < TOUCH_MIN_READ_MS) {
    gTouchIrq = false;
    return;
  }
  if (!dueToIrq && sinceLast < TOUCH_POLL_MS) return;
  gTouchIrq = false;
  lastPollMs = now;
  if (dueToIrq) gIrqCount++; else gPollCount++;

  uint16_t rx = 0, ry = 0;
  int8_t rc = touchRead(&rx, &ry);
  bool rawDown = (rc == RC_OK);
  bool down;
  int16_t sx = 0, sy = 0;

  if (rawDown) {
    missCount = 0;
    touchMap(rx, ry, &sx, &sy);
    lastRx = rx; lastRy = ry; lastSx = sx; lastSy = sy;
    down = true;
  } else if (gDown && missCount < TOUCH_MISS_TOLERANCE) {
    missCount++;
    /* v1.3: classify the miss instead of lumping it into one GLITCH count --
     * see the RC_* comment by touchRead(). Tallied both globally (for the
     * on-screen running totals) and per-hold (reported on this hold's UP
     * line below), so a single bad hold's signature is visible without
     * having to diff the running totals by hand. */
    if (rc == RC_I2C_ERR)      { gErrCount++;  holdErr++;  }
    else if (rc == RC_NO_TOUCH) { gZeroCount++; holdZero++; }
    else                        { gBadNCount++; holdBadN++; }
    down = true;
    rx = lastRx; ry = lastRy; sx = lastSx; sy = lastSy;
  } else {
    missCount = 0;
    down = false;
  }

  gRawX = rx; gRawY = ry;
  if (down) { gCurX = sx; gCurY = sy; }

  if (down && !gDown) {
    gPressStartX = sx; gPressStartY = sy;
    gDragging = false;
    gDragDist = 0;
    holdErr = holdZero = holdBadN = 0;
    pressedBtn = hitTestBtn(sx, sy);
    gTouchCount++;

    Serial.print(F("DOWN  raw=(")); Serial.print(rx); Serial.print(',');
    Serial.print(ry); Serial.print(F(") map=(")); Serial.print(sx);
    Serial.print(','); Serial.print(sy); Serial.print(F(") btn="));

    if (pressedBtn >= 0) {
      btnHitCount[pressedBtn]++;
      gBtnPressed[pressedBtn] = true;
      strncpy(gLastHitLabel, BTN_LABELS[pressedBtn], sizeof(gLastHitLabel) - 1);
      gLastHitLabel[sizeof(gLastHitLabel) - 1] = 0;
      gLastHitCount = btnHitCount[pressedBtn];
      drawButton((uint8_t)pressedBtn, true);
      Serial.println(BTN_LABELS[pressedBtn]);
      if (pressedBtn == BTN_RESET) resetStats();
    } else {
      strncpy(gLastHitLabel, "-", sizeof(gLastHitLabel));
      Serial.println(F("none"));
    }
  } else if (down && gDown) {
    int16_t dx = (int16_t)(sx - gPressStartX), dy = (int16_t)(sy - gPressStartY);
    gDragDist = (int16_t)sqrtf((float)(dx * dx + dy * dy));
    if (!gDragging && gDragDist >= DRAG_THRESHOLD_PX) {
      gDragging = true;
      Serial.println(F("DRAG START"));
    }
  } else if (!down && gDown) {
    if (pressedBtn >= 0) {
      gBtnPressed[pressedBtn] = false;
      drawButton((uint8_t)pressedBtn, false);
    }
    Serial.print(F("UP    map=(")); Serial.print(gCurX); Serial.print(',');
    Serial.print(gCurY); Serial.print(F(") dragDist="));
    Serial.print(gDragDist);
    Serial.print(gDragging ? F(" (was dragging)") : F(""));
    /* v1.3: what actually killed this hold, if it was a hold at all --
     * miss[err=I2C bus failures, zero=chip cleanly said no touch,
     * badN=garbage touch count]. All zero means it ended on a real,
     * immediate release (rawDown simply went false and stayed false),
     * not on exhausting the miss-tolerance debounce. */
    Serial.print(F(" miss[err=")); Serial.print(holdErr);
    Serial.print(F(",zero=")); Serial.print(holdZero);
    Serial.print(F(",badN=")); Serial.print(holdBadN);
    Serial.println(F("]"));
    pressedBtn = -1;
    gDragging = false;
  }

  gDown = down;
  updateCrosshair(down ? sx : (int16_t)-1, down ? sy : (int16_t)-1, gDragging);
}

/* ==========================================================================
 * Status text — five lines above the divider, each with its own
 * change-detection cache so an unchanged field costs nothing per tick.
 * ========================================================================*/
static void updateStatusFields() {
  char buf[64];

  static char cFps[12] = "";
  snprintf(buf, sizeof(buf), "FPS:%3lu", (unsigned long)gFps);
  drawField(380, 2, 10, 1, COL_TEXT_HI, COL_BG, buf, cFps);

  static char cL2[40] = "";
  snprintf(buf, sizeof(buf), "RAW:(%4u,%4u) MAP:(%4d,%4d)", gRawX, gRawY, gCurX, gCurY);
  drawField(4, 14, 36, 1, COL_TEXT, COL_BG, buf, cL2);

  static char cL3[36] = "";
  snprintf(buf, sizeof(buf), "STATE:%-4s DRAG:%-3s DIST:%3d",
           gDown ? "DOWN" : "UP", gDragging ? "YES" : "NO", gDragDist);
  drawField(4, 26, 32, 1, COL_TEXT, COL_BG, buf, cL3);

  /* v1.3: GLITCH split into ERR (I2C transport failure) / Z (chip cleanly
   * says zero touches) / N (garbage touch count) -- see the RC_* comment
   * by touchRead(). Widened from the old single GLITCH field accordingly. */
  static char cL4[60] = "";
  snprintf(buf, sizeof(buf), "TOUCHES:%4lu ERR:%3lu Z:%3lu N:%3lu IRQ:%5lu POLL:%5lu",
           (unsigned long)gTouchCount, (unsigned long)gErrCount,
           (unsigned long)gZeroCount, (unsigned long)gBadNCount,
           (unsigned long)gIrqCount, (unsigned long)gPollCount);
  drawField(4, 38, 56, 1, COL_TEXT, COL_BG, buf, cL4);

  static char cL5[32] = "";
  snprintf(buf, sizeof(buf), "LAST HIT: %-6s (#%lu)", gLastHitLabel, (unsigned long)gLastHitCount);
  drawField(4, 50, 28, 1, COL_TEXT, COL_BG, buf, cL5);
}

/* ==========================================================================
 * Screen paint — static chrome painted exactly once at boot. Everything
 * that changes afterward goes through updateStatusFields()/drawButton()/
 * updateCrosshair(), never a repaint of the whole screen (a full 480x320
 * fillScreen() costs ~150-200ms at this SPI clock and would both throttle
 * this screen's own FPS number and stall touch polling while it runs —
 * exactly the thing this sketch exists to measure honestly).
 * ========================================================================*/
static void paintOnce() {
  gfx->fillScreen(COL_BG);
  gfx->setTextWrap(false);

  gfx->setTextSize(1);
  gfx->setTextColor(COL_TEXT_HI);
  gfx->setCursor(4, 2);
  gfx->print("TOUCH DEV TEST v1.3");

  gfx->drawFastHLine(0, STATUS_DIVIDER_Y, LCD_W, COL_GRID);

  for (uint8_t i = 0; i < BTN_N; i++) drawButton(i, false);
}

/* ==========================================================================
 * setup() / loop()
 * ========================================================================*/
void setup() {
  /* SD_CS must never float low — first GPIO touched, same as display_bringup */
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_LCD_LED, OUTPUT); digitalWrite(PIN_LCD_LED, HIGH);  /* solid — never PWM'd */

  Serial.begin(115200);

  gfx->begin(SPI_HZ);
  touchInit();

  Serial.println(F("Touch Dev Test v1.3 -- 115200 baud, Newline line ending"));

  paintOnce();
}

void loop() {
  static uint32_t frameCount = 0, lastFpsMs = 0;
  uint32_t now = millis();
  frameCount++;
  if (now - lastFpsMs >= 1000) {
    gFps = frameCount;
    frameCount = 0;
    lastFpsMs = now;
  }

  pollTouch();
  updateStatusFields();
}
