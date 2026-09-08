/* ==========================================================================
 * FABLE_DEV_TEST_SCREEN.ino — v1.1 (2026-09-08)
 *
 * From-scratch touchscreen reliability test firmware.
 * Target: RP2040-Zero (arduino-pico / earlephilhower core), 3.5" IPS SPI
 * module (lcdwiki MSP3525/MSP3526): ST7796 panel, 480x320 landscape,
 * FT6336U capacitive touch on I2C.
 *
 * ZERO libraries beyond the core. Every byte that goes over SPI or I2C is
 * written out in this file — nothing hidden, nothing to misconfigure.
 *
 * --------------------------------------------------------------------------
 * ARCHITECTURE — read this before adding features
 *
 *   CORE 1 owns touch, and nothing else. loop1() polls the FT6336U over
 *   I2C1 at a fixed TOUCH_HZ cadence (FT6336U in POLLING mode, 0xA4=0x00;
 *   the INT pin is only *counted* as a diagnostic, nothing waits on it).
 *   Core 1 runs a press/release state machine, validates every sample,
 *   and publishes:
 *     - the latest sample via a seqlock-guarded shared struct (g_touch)
 *     - discrete DOWN/UP events via the hardware inter-core FIFO
 *
 *   CORE 0 owns the display and all UI logic. It drains the event FIFO
 *   every pass of loop() and repaints incrementally.
 *
 *   THE CONTRACT (this is what keeps touch responsive forever):
 *     1. Core 1's loop must never block, never print, never draw. Its only
 *        job is the ~120 us I2C read + bookkeeping, every 1/TOUCH_HZ s.
 *     2. Core 0 may be arbitrarily slow — a slow frame delays *reaction*,
 *        never *registration*. Events queue in the FIFO (8 deep; overflow
 *        is counted, not silent). Press the LOAD button to prove it: it
 *        injects 50 ms of blocking delay per frame and the touch sample
 *        rate does not move, and no tap is lost.
 *     3. Never call fillScreen()/full-frame repaints per tick. Paint
 *        static chrome once, repaint only what changed (this file's
 *        statsLine cache shows the pattern).
 *     4. If the real product later needs core 1 for ADC sampling, fold the
 *        touch poll into that same loop (budget ~120 us per sample) —
 *        do NOT move touch back to core 0.
 *
 * --------------------------------------------------------------------------
 * WIRING FACTS
 *   - SD_CS (GP9) is parked HIGH — it shares the FPC's MISO line.
 *   - Backlight is driven solid HIGH, never PWM (PWM injects noise into
 *     the analog front end on the real board).
 *   - I2C1 on GP10/GP11, 400 kHz. FT6336U at address 0x38.
 *
 * TOUCH MAPPING
 *   Panel raw frame is portrait 320(x) x 480(y). For landscape rotation
 *   the default here is SWAP_XY + INVERT_Y (the usual ST7796+FT6336U
 *   landscape mapping). The stats panel shows RAW and MAP side by side and
 *   the crosshair is ground truth: if the crosshair mirrors your finger,
 *   flip TOUCH_INV_X / TOUCH_INV_Y below — one line, no other changes.
 *
 * SERIAL: 115200. Events stream as they happen; 'r' resets stats.
 * ========================================================================*/

#include <SPI.h>
#include <Wire.h>

/* ==========================================================================
 * Pin map — identical to display_bringup.ino / logging_current_meter_ui.ino
 * ========================================================================*/
#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_RS    6      /* DC: data/command select */
#define PIN_LCD_RST   7
#define PIN_LCD_LED   8
#define PIN_SD_CS     9      /* must be parked HIGH — shares the FPC's MISO */
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13

/* ==========================================================================
 * Tunables
 * ========================================================================*/
#define LCD_W          480
#define LCD_H          320
#define SPI_HZ         40000000UL  /* ST7796 takes this happily; drop to
                                      20 MHz if you ever suspect the wiring */
#define LCD_MADCTL     0x28        /* landscape: MV | BGR */
#define LCD_INVERT     0           /* 1 -> send INVON (0x21). Panel-dependent;
                                      check the R/G/B/W swatches bottom-right */

#define TOUCH_HZ       200         /* fixed core-1 sample cadence */
#define TOUCH_I2C_HZ   400000
#define FT_ADDR        0x38
#define TOUCH_SWAP_XY  1
#define TOUCH_INV_X    0
#define TOUCH_INV_Y    1
#define TOUCH_HIT_SLOP 4           /* px of forgiveness around every button */
#define RELEASE_CONFIRM 2          /* consecutive empty reads before UP; a
                                      single empty read mid-touch = DROP glitch */
#define JUMP_PX        60          /* same-contact move > this in one sample
                                      (5 ms) = JMP glitch */
#define TAP_MS         300         /* DOWN->UP faster than this + <12 px move */
#define TAP_MOVE_PX    12
#define LOAD_BLOCK_MS  50          /* artificial per-frame load, LOAD button */

/* ==========================================================================
 * Colors (RGB565)
 * ========================================================================*/
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_ORANGE  0xFD20
#define C_GREY    0x8410
#define C_DGREY   0x39E7
#define C_BG      0x10A2   /* near-black blue-grey */
#define C_PANEL   0x2124
#define C_ACCENT  0x05FF

/* ==========================================================================
 * 5x7 font, column-major, bit0 = top row. ASCII 32..95 (uppercase set).
 * Generated + visually verified glyph-by-glyph; unmapped codes render '-'.
 * ========================================================================*/
static const uint8_t FONT5x7[64][5] = {
  0x00, 0x00, 0x00, 0x00, 0x00,  /* ' ' */
  0x00, 0x00, 0x5F, 0x00, 0x00,  /* '!' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '"' */
  0x14, 0x7F, 0x14, 0x7F, 0x14,  /* '#' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '$' */
  0x63, 0x13, 0x08, 0x64, 0x63,  /* '%' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '&' */
  0x00, 0x00, 0x07, 0x00, 0x00,  /* "'" */
  0x00, 0x1C, 0x22, 0x41, 0x00,  /* '(' */
  0x00, 0x41, 0x22, 0x1C, 0x00,  /* ')' */
  0x2A, 0x1C, 0x3E, 0x1C, 0x2A,  /* '*' */
  0x08, 0x08, 0x3E, 0x08, 0x08,  /* '+' */
  0x00, 0x50, 0x30, 0x00, 0x00,  /* ',' */
  0x00, 0x08, 0x08, 0x08, 0x00,  /* '-' */
  0x00, 0x60, 0x60, 0x00, 0x00,  /* '.' */
  0x60, 0x10, 0x08, 0x04, 0x03,  /* '/' */
  0x3E, 0x51, 0x49, 0x45, 0x3E,  /* '0' */
  0x00, 0x42, 0x7F, 0x40, 0x00,  /* '1' */
  0x42, 0x61, 0x51, 0x49, 0x46,  /* '2' */
  0x22, 0x41, 0x49, 0x49, 0x36,  /* '3' */
  0x18, 0x14, 0x12, 0x7F, 0x10,  /* '4' */
  0x27, 0x45, 0x45, 0x45, 0x39,  /* '5' */
  0x3C, 0x4A, 0x49, 0x49, 0x30,  /* '6' */
  0x01, 0x01, 0x79, 0x05, 0x03,  /* '7' */
  0x36, 0x49, 0x49, 0x49, 0x36,  /* '8' */
  0x06, 0x49, 0x49, 0x29, 0x1E,  /* '9' */
  0x00, 0x00, 0x36, 0x00, 0x00,  /* ':' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* ';' */
  0x08, 0x14, 0x22, 0x41, 0x00,  /* '<' */
  0x14, 0x14, 0x14, 0x14, 0x14,  /* '=' */
  0x00, 0x41, 0x22, 0x14, 0x08,  /* '>' */
  0x02, 0x01, 0x51, 0x09, 0x06,  /* '?' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '@' */
  0x7E, 0x09, 0x09, 0x09, 0x7E,  /* 'A' */
  0x7F, 0x49, 0x49, 0x49, 0x36,  /* 'B' */
  0x3E, 0x41, 0x41, 0x41, 0x22,  /* 'C' */
  0x7F, 0x41, 0x41, 0x41, 0x3E,  /* 'D' */
  0x7F, 0x49, 0x49, 0x49, 0x41,  /* 'E' */
  0x7F, 0x09, 0x09, 0x09, 0x01,  /* 'F' */
  0x3E, 0x41, 0x49, 0x49, 0x3A,  /* 'G' */
  0x7F, 0x08, 0x08, 0x08, 0x7F,  /* 'H' */
  0x00, 0x41, 0x7F, 0x41, 0x00,  /* 'I' */
  0x20, 0x40, 0x41, 0x3F, 0x01,  /* 'J' */
  0x7F, 0x08, 0x14, 0x22, 0x41,  /* 'K' */
  0x7F, 0x40, 0x40, 0x40, 0x40,  /* 'L' */
  0x7F, 0x02, 0x0C, 0x02, 0x7F,  /* 'M' */
  0x7F, 0x02, 0x04, 0x08, 0x7F,  /* 'N' */
  0x3E, 0x41, 0x41, 0x41, 0x3E,  /* 'O' */
  0x7F, 0x09, 0x09, 0x09, 0x06,  /* 'P' */
  0x3E, 0x41, 0x51, 0x21, 0x5E,  /* 'Q' */
  0x7F, 0x09, 0x19, 0x29, 0x46,  /* 'R' */
  0x46, 0x49, 0x49, 0x49, 0x31,  /* 'S' */
  0x01, 0x01, 0x7F, 0x01, 0x01,  /* 'T' */
  0x3F, 0x40, 0x40, 0x40, 0x3F,  /* 'U' */
  0x1F, 0x20, 0x40, 0x20, 0x1F,  /* 'V' */
  0x7F, 0x20, 0x18, 0x20, 0x7F,  /* 'W' */
  0x63, 0x14, 0x08, 0x14, 0x63,  /* 'X' */
  0x03, 0x04, 0x78, 0x04, 0x03,  /* 'Y' */
  0x61, 0x51, 0x49, 0x45, 0x43,  /* 'Z' */
  0x00, 0x00, 0x7F, 0x41, 0x00,  /* '[' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '\\' */
  0x00, 0x41, 0x7F, 0x00, 0x00,  /* ']' */
  0x08, 0x08, 0x08, 0x08, 0x08,  /* '^' */
  0x40, 0x40, 0x40, 0x40, 0x40,  /* '_' */
};

/* ==========================================================================
 * Small types + manual prototypes.
 * (Manual prototypes matter: the Arduino IDE auto-generates prototypes
 * ABOVE these type definitions, which fails to compile for any function
 * taking one of these types. Keep them in sync if you add such functions.)
 * ========================================================================*/
struct Rect { int16_t x, y, w, h; };

struct Btn {
  Rect        r;
  const char *label;
  uint16_t    color;
  uint32_t    hits;
  bool        pressed;   /* visually latched down right now */
};

static bool rectHit(const Rect &r, int16_t x, int16_t y, int16_t slop);
static void drawBtn(const Btn &b);

/* ==========================================================================
 * ST7796 display driver — raw SPI, no library
 * ========================================================================*/
static uint16_t s_lineBuf[LCD_W];   /* one scanline of RGB565 */

static inline void lcdCsLow()   { digitalWrite(PIN_LCD_CS, LOW);  }
static inline void lcdCsHigh()  { digitalWrite(PIN_LCD_CS, HIGH); }
static inline void lcdDcCmd()   { digitalWrite(PIN_LCD_RS, LOW);  }
static inline void lcdDcData()  { digitalWrite(PIN_LCD_RS, HIGH); }

static void lcdCmd(uint8_t c) {
  lcdDcCmd();
  SPI.transfer(c);
  lcdDcData();
}

static void lcdCmdN(uint8_t c, const uint8_t *d, size_t n) {
  lcdCmd(c);
  if (n) SPI.transfer(d, nullptr, n);
}

/* Convenience for short parameter lists */
static void lcdCmd1(uint8_t c, uint8_t a)            { uint8_t d[1] = {a};    lcdCmdN(c, d, 1); }

static void lcdBegin() {
  pinMode(PIN_LCD_CS,  OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
  pinMode(PIN_LCD_RS,  OUTPUT); digitalWrite(PIN_LCD_RS, HIGH);
  pinMode(PIN_LCD_RST, OUTPUT);
  pinMode(PIN_LCD_LED, OUTPUT); digitalWrite(PIN_LCD_LED, LOW);
  pinMode(PIN_SD_CS,   OUTPUT); digitalWrite(PIN_SD_CS, HIGH);   /* park! */

  SPI.setSCK(PIN_SCK);
  SPI.setTX(PIN_MOSI);
  SPI.setRX(PIN_MISO);
  SPI.begin();

  /* Hard reset */
  digitalWrite(PIN_LCD_RST, HIGH); delay(10);
  digitalWrite(PIN_LCD_RST, LOW);  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH); delay(120);

  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  lcdCsLow();

  lcdCmd(0x01); delay(120);                          /* SWRESET  */
  lcdCmd(0x11); delay(120);                          /* SLPOUT   */
  lcdCmd1(0xF0, 0xC3);                               /* CSC unlock pt 1 */
  lcdCmd1(0xF0, 0x96);                               /* CSC unlock pt 2 */
  lcdCmd1(0x36, LCD_MADCTL);                         /* MADCTL   */
  lcdCmd1(0x3A, 0x55);                               /* 16-bit color */
  lcdCmd1(0xB4, 0x01);                               /* 1-dot inversion */
  { const uint8_t d[] = {0x80, 0x02, 0x3B};          /* DFC */
    lcdCmdN(0xB6, d, sizeof d); }
  { const uint8_t d[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
    lcdCmdN(0xE8, d, sizeof d); }                    /* DOCA */
  lcdCmd1(0xC1, 0x06);                               /* power ctl 2 */
  lcdCmd1(0xC2, 0xA7);                               /* power ctl 3 */
  lcdCmd1(0xC5, 0x18);                               /* VCOM */
  delay(120);
  { const uint8_t d[] = {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
                         0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B};
    lcdCmdN(0xE0, d, sizeof d); }                    /* +gamma */
  { const uint8_t d[] = {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
                         0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B};
    lcdCmdN(0xE1, d, sizeof d); }                    /* -gamma */
  delay(120);
  lcdCmd1(0xF0, 0x3C);                               /* CSC lock */
  lcdCmd1(0xF0, 0x69);
  delay(120);
#if LCD_INVERT
  lcdCmd(0x21);                                      /* INVON */
#else
  lcdCmd(0x20);                                      /* INVOFF */
#endif
  lcdCmd(0x29); delay(20);                           /* DISPON */

  lcdCsHigh();
  SPI.endTransaction();

  digitalWrite(PIN_LCD_LED, HIGH);   /* backlight: solid HIGH, never PWM */
}

static void lcdWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
  uint8_t d[4];
  d[0] = x >> 8; d[1] = x & 0xFF;
  d[2] = (x + w - 1) >> 8; d[3] = (x + w - 1) & 0xFF;
  lcdCmdN(0x2A, d, 4);
  d[0] = y >> 8; d[1] = y & 0xFF;
  d[2] = (y + h - 1) >> 8; d[3] = (y + h - 1) & 0xFF;
  lcdCmdN(0x2B, d, 4);
  lcdCmd(0x2C);
}

static void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  uint16_t be = __builtin_bswap16(c);
  for (int i = 0; i < w; i++) s_lineBuf[i] = be;

  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  lcdCsLow();
  lcdWindow(x, y, w, h);
  for (int row = 0; row < h; row++)
    SPI.transfer((uint8_t *)s_lineBuf, nullptr, (size_t)w * 2);
  lcdCsHigh();
  SPI.endTransaction();
}

static void drawFrame(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  fillRect(x, y, w, 1, c);
  fillRect(x, y + h - 1, w, 1, c);
  fillRect(x, y, 1, h, c);
  fillRect(x + w - 1, y, 1, h, c);
}

/* Text: scale-able 5x7, fixed 6*scale advance. fg on bg (opaque cell). */
static void drawText(int16_t x, int16_t y, const char *s,
                     uint16_t fg, uint16_t bg, uint8_t scale) {
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  lcdCsLow();
  uint16_t feBe = __builtin_bswap16(fg), bgBe = __builtin_bswap16(bg);
  for (; *s; s++, x += 6 * scale) {
    char ch = *s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;         /* uppercase-only font */
    uint8_t idx = (ch < 32 || ch > 95) ? ('?' - 32) : (ch - 32);
    const uint8_t *col = FONT5x7[idx];
    if (x + 6 * scale > LCD_W || y + 8 * scale > LCD_H || x < 0 || y < 0)
      continue;
    lcdWindow(x, y, 6 * scale, 8 * scale);
    /* stream cell row by row */
    for (int ry = 0; ry < 8; ry++) {
      for (int sy = 0; sy < scale; sy++) {
        uint16_t *p = s_lineBuf;
        for (int cx = 0; cx < 6; cx++) {
          bool on = (cx < 5) && (ry < 7) && ((col[cx] >> ry) & 1);
          uint16_t v = on ? feBe : bgBe;
          for (int sx = 0; sx < scale; sx++) *p++ = v;
        }
        SPI.transfer((uint8_t *)s_lineBuf, nullptr, (size_t)6 * scale * 2);
      }
    }
  }
  lcdCsHigh();
  SPI.endTransaction();
}

static int16_t textW(const char *s, uint8_t scale) {
  return (int16_t)strlen(s) * 6 * scale;
}

/* ==========================================================================
 * Touch — FT6336U on I2C1, POLLING mode. ALL of this runs on CORE 1.
 * ========================================================================*/

/* --- shared state: core 1 writes, core 0 reads (seqlock) --- */
static volatile uint32_t g_seq = 0;
static volatile struct {
  uint16_t rawX, rawY;     /* last valid raw coords (portrait frame)      */
  uint8_t  pts;            /* current touch point count (0..2)            */
  bool     down;           /* debounced contact state                     */
  uint32_t tDownUs;        /* micros() at last confirmed DOWN sample      */
  uint32_t heldMs;         /* duration of current/last contact            */
} g_touch;

/* --- counters: core 1 (or ISR) increments, core 0 reads/displays.
 *     32-bit aligned loads/stores are atomic on RP2040, so plain
 *     volatile is sufficient for independent counters. --- */
static volatile uint32_t g_cntDown = 0, g_cntUp = 0;
static volatile uint32_t g_glRange = 0, g_glJump = 0, g_glCount = 0,
                         g_glDrop = 0, g_glOvf = 0, g_i2cErr = 0;
static volatile uint32_t g_intEdges = 0;
static volatile uint32_t g_touchHz = 0;         /* achieved sample rate  */
static volatile uint8_t  g_ftChip[3] = {0,0,0}; /* 0xA3 chip, 0xA6 fw, 0xA8 vendor */
static volatile bool     g_ftOk = false;
static volatile bool     g_statReset = false;   /* core 0 sets, core 1 clears */
static volatile uint8_t  g_maxPts = 0;

/* FIFO event packing: [31:28]=type, [23:12]=x, [11:0]=y (RAW coords) */
#define EV_DOWN 1u
#define EV_UP   2u
static inline uint32_t evPack(uint32_t t, uint32_t x, uint32_t y) {
  return (t << 28) | ((x & 0xFFF) << 12) | (y & 0xFFF);
}

static void ctpIntIsr() { g_intEdges++; }   /* diagnostic only */

static bool ftWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(FT_ADDR);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

static bool ftRead(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire1.beginTransmission(FT_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;   /* repeated start */
  if (Wire1.requestFrom((uint8_t)FT_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire1.read();
  return true;
}

/* ---------------- core 1 entry points ---------------- */
void setup1() {
  pinMode(PIN_CTP_RST, OUTPUT);
  pinMode(PIN_CTP_INT, INPUT_PULLUP);

  digitalWrite(PIN_CTP_RST, LOW);  delay(10);
  digitalWrite(PIN_CTP_RST, HIGH); delay(300);   /* FT boot time */

  Wire1.setSDA(PIN_CTP_SDA);
  Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin();
  Wire1.setClock(TOUCH_I2C_HZ);

  bool ok = true;
  ok &= ftWrite(0x00, 0x00);   /* DEV_MODE: normal operating mode        */
  ok &= ftWrite(0xA4, 0x00);   /* G_MODE: POLLING (0x01 = trigger mode)  */
  uint8_t id;
  if (ftRead(0xA3, &id, 1)) g_ftChip[0] = id; else ok = false;
  if (ftRead(0xA6, &id, 1)) g_ftChip[1] = id;
  if (ftRead(0xA8, &id, 1)) g_ftChip[2] = id;
  g_ftOk = ok;

  attachInterrupt(digitalPinToInterrupt(PIN_CTP_INT), ctpIntIsr, FALLING);
}

void loop1() {
  static uint32_t nextUs = 0;
  static uint32_t sampleCount = 0, hzWindowMs = 0;
  static bool     down = false;
  static uint8_t  emptyRun = 0;
  static uint16_t lastX = 0, lastY = 0;
  static uint32_t tDownUs = 0;

  /* fixed-cadence pacing (no drift, no blocking) */
  uint32_t now = micros();
  if (nextUs == 0) nextUs = now;
  if ((int32_t)(now - nextUs) < 0) return;      /* not yet due */
  nextUs += 1000000UL / TOUCH_HZ;
  if ((int32_t)(micros() - nextUs) > 0)          /* fell behind: resync */
    nextUs = micros();

  if (g_statReset) {
    g_cntDown = g_cntUp = 0;
    g_glRange = g_glJump = g_glCount = g_glDrop = g_glOvf = g_i2cErr = 0;
    g_intEdges = 0; g_maxPts = 0;
    g_statReset = false;
  }

  /* --- the actual sample: one burst read, 0x02..0x06 --- */
  uint8_t b[5];
  bool ok = ftRead(0x02, b, 5);
  sampleCount++;

  uint32_t nowMs = millis();
  if (nowMs - hzWindowMs >= 1000) {
    g_touchHz = sampleCount;
    sampleCount = 0;
    hzWindowMs = nowMs;
  }

  if (!ok) { g_i2cErr++; return; }

  uint8_t pts = b[0] & 0x0F;
  if (pts > 2) { g_glCount++; return; }           /* impossible count */
  if (pts > g_maxPts) g_maxPts = pts;

  if (pts == 0) {
    /* possible release — require RELEASE_CONFIRM in a row */
    if (down) {
      if (++emptyRun >= RELEASE_CONFIRM) {
        down = false; emptyRun = 0;
        g_cntUp++;
        if (!rp2040.fifo.push_nb(evPack(EV_UP, lastX, lastY))) g_glOvf++;
      }
    } else emptyRun = 0;
  } else {
    uint16_t x = ((uint16_t)(b[1] & 0x0F) << 8) | b[2];
    uint16_t y = ((uint16_t)(b[3] & 0x0F) << 8) | b[4];

    if (x > 319 || y > 479) { g_glRange++; return; }   /* out of panel */

    if (down && emptyRun) g_glDrop++;   /* single-sample dropout mid-touch */
    emptyRun = 0;

    if (down) {
      int16_t dx = (int16_t)x - (int16_t)lastX;
      int16_t dy = (int16_t)y - (int16_t)lastY;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      if (dx > JUMP_PX || dy > JUMP_PX) g_glJump++;
    } else {
      down = true;
      tDownUs = micros();
      g_cntDown++;
      if (!rp2040.fifo.push_nb(evPack(EV_DOWN, x, y))) g_glOvf++;
    }
    lastX = x; lastY = y;
  }

  /* publish latest state (seqlock: odd = write in progress) */
  g_seq++;
  g_touch.rawX   = lastX;
  g_touch.rawY   = lastY;
  g_touch.pts    = pts;
  g_touch.down   = down;
  g_touch.tDownUs= tDownUs;
  g_touch.heldMs = down ? (micros() - tDownUs) / 1000 : g_touch.heldMs;
  g_seq++;
}

/* ==========================================================================
 * Everything below runs on CORE 0 — UI, stats, serial.
 * ========================================================================*/

/* --- coherent snapshot of the shared touch state --- */
struct TouchSnap {
  uint16_t rawX, rawY;
  uint8_t  pts;
  bool     down;
  uint32_t tDownUs;
  uint32_t heldMs;
};
static void takeSnap(TouchSnap &s);   /* manual prototype (custom type) */

static void takeSnap(TouchSnap &s) {
  uint32_t s1, s2;
  do {
    s1 = g_seq;
    s.rawX = g_touch.rawX;  s.rawY = g_touch.rawY;
    s.pts  = g_touch.pts;   s.down = g_touch.down;
    s.tDownUs = g_touch.tDownUs;
    s.heldMs  = g_touch.heldMs;
    s2 = g_seq;
  } while (s1 != s2 || (s1 & 1));
}

/* --- raw (portrait 320x480) -> screen (landscape 480x320) --- */
static void mapTouch(uint16_t rx, uint16_t ry, int16_t &mx, int16_t &my) {
#if TOUCH_SWAP_XY
  mx = ry;  my = rx;
#else
  mx = rx;  my = ry;
#endif
#if TOUCH_INV_X
  mx = (LCD_W - 1) - mx;
#endif
#if TOUCH_INV_Y
  my = (LCD_H - 1) - my;
#endif
  if (mx < 0) mx = 0;
  if (mx >= LCD_W) mx = LCD_W - 1;
  if (my < 0) my = 0;
  if (my >= LCD_H) my = LCD_H - 1;
}

/* ==========================================================================
 * Layout
 * ========================================================================*/
static const Rect PAD = {2, 24, 256, 294};    /* crosshair playground */

#define NBTN 10
static Btn s_btn[NBTN] = {
  /* big targets */
  { {262, 200, 68, 44}, "A",    C_GREEN,  0, false },
  { {336, 200, 68, 44}, "B",    C_CYAN,   0, false },
  { {410, 200, 66, 44}, "LOAD", C_ORANGE, 0, false },
  /* small targets — the known trouble case */
  { {262, 254, 26, 22}, "S1",   C_YELLOW, 0, false },
  { {296, 254, 26, 22}, "S2",   C_YELLOW, 0, false },
  { {330, 254, 26, 22}, "S3",   C_YELLOW, 0, false },
  { {364, 254, 26, 22}, "S4",   C_YELLOW, 0, false },
  /* pathological target */
  { {398, 257, 16, 16}, "T",    C_RED,    0, false },
  /* controls */
  { {424, 250, 52, 30}, "RST",  C_WHITE,  0, false },
  /* invisible pad "button" so pad hits report a name; keep last */
  { {  2,  24,256,294}, "PAD",  C_GREY,   0, false },
};
#define BTN_LOAD 2
#define BTN_RST  8
#define BTN_PAD  9

static bool rectHit(const Rect &r, int16_t x, int16_t y, int16_t slop) {
  return x >= r.x - slop && x < r.x + r.w + slop &&
         y >= r.y - slop && y < r.y + r.h + slop;
}

static bool s_loadOn = false;

static void drawBtn(const Btn &b) {
  if (&b == &s_btn[BTN_PAD]) return;              /* pad isn't drawn here */
  bool on = (&b == &s_btn[BTN_LOAD]) && s_loadOn;
  uint16_t fill = b.pressed ? C_ACCENT : (on ? C_ORANGE : C_DGREY);
  uint16_t txt  = b.pressed ? C_BLACK  : (on ? C_BLACK  : b.color);
  fillRect(b.r.x, b.r.y, b.r.w, b.r.h, fill);
  drawFrame(b.r.x, b.r.y, b.r.w, b.r.h, b.color);
  uint8_t sc = (b.r.h >= 40) ? 2 : 1;
  int16_t tx = b.r.x + (b.r.w - textW(b.label, sc)) / 2;
  int16_t ty = b.r.y + (b.r.h - 8 * sc) / 2 + 1;
  drawText(tx, ty, b.label, txt, fill, sc);
  if (b.r.h >= 40) {                              /* hit count in big btns */
    char n[8]; snprintf(n, sizeof n, "%lu", (unsigned long)b.hits);
    drawText(b.r.x + 3, b.r.y + b.r.h - 10, n, C_GREY, fill, 1);
  }
}

/* ==========================================================================
 * Stats panel — cached lines, only changed lines repaint
 * ========================================================================*/
#define STAT_X     262
#define STAT_Y      28
#define STAT_LH     12
#define STAT_NL     12
#define STAT_COLS   36
static char s_statCache[STAT_NL][STAT_COLS + 1];

static void statsLine(uint8_t i, const char *fmt, ...) {
  char buf[STAT_COLS + 1];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  /* pad to full width so shorter values erase longer old ones */
  size_t l = strlen(buf);
  while (l < STAT_COLS) buf[l++] = ' ';
  buf[STAT_COLS] = 0;
  if (i >= STAT_NL || strcmp(buf, s_statCache[i]) == 0) return;
  strcpy(s_statCache[i], buf);
  drawText(STAT_X, STAT_Y + i * STAT_LH, buf, C_WHITE, C_BG, 1);
}

/* ==========================================================================
 * Core-0 UI state
 * ========================================================================*/
static int16_t  s_chX = -1, s_chY = -1;       /* live crosshair pos       */
static int16_t  s_mkX = -1, s_mkY = -1;       /* last-DOWN marker         */
static int16_t  s_dnX = 0,  s_dnY = 0;        /* mapped DOWN coords       */
static uint32_t s_dnMs = 0;
static uint32_t s_taps = 0;
static uint32_t s_latUs = 0;
static char     s_lastBtn[8] = "-";
static uint32_t s_frames = 0, s_uiHz = 0;

static void eraseCross() {
  if (s_chX < 0) return;
  fillRect(s_chX - 14, s_chY - 14, 29, 29, C_BLACK);
  /* marker may have been overlapped */
  if (s_mkX >= 0 && abs(s_mkX - s_chX) <= 16 && abs(s_mkY - s_chY) <= 16)
    fillRect(s_mkX - 1, s_mkY - 1, 3, 3, C_RED);
  s_chX = -1;
}

static void drawCross(int16_t x, int16_t y) {
  /* keep fully inside pad interior */
  int16_t lo = 16;
  if (x < PAD.x + lo) x = PAD.x + lo;
  if (x > PAD.x + PAD.w - 1 - lo) x = PAD.x + PAD.w - 1 - lo;
  if (y < PAD.y + lo) y = PAD.y + lo;
  if (y > PAD.y + PAD.h - 1 - lo) y = PAD.y + PAD.h - 1 - lo;
  if (x == s_chX && y == s_chY) return;
  eraseCross();
  /* big + bold: 29 px arms, 3 px thick, white core for contrast */
  fillRect(x - 14, y - 1, 29, 3, C_GREEN);
  fillRect(x - 1, y - 14, 3, 29, C_GREEN);
  fillRect(x - 14, y, 29, 1, C_WHITE);
  fillRect(x, y - 14, 1, 29, C_WHITE);
  s_chX = x; s_chY = y;
}

static void setMarker(int16_t x, int16_t y) {
  if (s_mkX >= 0) fillRect(s_mkX - 1, s_mkY - 1, 3, 3, C_BLACK);
  if (x > PAD.x + 2 && x < PAD.x + PAD.w - 3 &&
      y > PAD.y + 2 && y < PAD.y + PAD.h - 3) {
    fillRect(x - 1, y - 1, 3, 3, C_RED);
    s_mkX = x; s_mkY = y;
  } else s_mkX = -1;
}

static void resetStats() {
  g_statReset = true;               /* core 1 zeroes its counters */
  s_taps = 0; s_latUs = 0;
  strcpy(s_lastBtn, "-");
  for (int i = 0; i < NBTN; i++) { s_btn[i].hits = 0; }
  for (int i = 0; i < 3; i++) drawBtn(s_btn[i]);
  Serial.println("[STATS RESET]");
}

/* ==========================================================================
 * Chrome (painted once)
 * ========================================================================*/
static void paintChrome() {
  fillRect(0, 0, LCD_W, LCD_H, C_BG);
  fillRect(0, 0, LCD_W, 22, C_PANEL);
  drawText(4, 4, "FABLE TOUCH TEST V1.1", C_ACCENT, C_PANEL, 2);

  fillRect(PAD.x, PAD.y, PAD.w, PAD.h, C_BLACK);
  drawFrame(PAD.x, PAD.y, PAD.w, PAD.h, C_DGREY);
  drawText(PAD.x + 6, PAD.y + 4, "TOUCH ANYWHERE - CROSSHAIR = TRUTH",
           C_GREY, C_BLACK, 1);

  for (int i = 0; i < NBTN; i++) drawBtn(s_btn[i]);

  /* color truth swatches: if these four don't read R G B W, flip LCD_INVERT */
  const uint16_t sw[4] = {C_RED, C_GREEN, C_BLUE, C_WHITE};
  const char    *sl[4] = {"R", "G", "B", "W"};
  for (int i = 0; i < 4; i++) {
    int16_t x = 262 + i * 40;
    fillRect(x, 288, 30, 26, sw[i]);
    drawFrame(x, 288, 30, 26, C_GREY);
    drawText(x + 12, 297, sl[i], (i == 3) ? C_BLACK : C_WHITE, sw[i], 1);
  }
  memset(s_statCache, 0, sizeof s_statCache);
}

/* ==========================================================================
 * Event handling
 * ========================================================================*/
static void onDown(int16_t mx, int16_t my, uint32_t tDownUs) {
  s_dnX = mx; s_dnY = my; s_dnMs = millis();
  s_latUs = micros() - tDownUs;
  setMarker(mx, my);

  /* first matching real button wins; PAD is the catch-all */
  int hit = -1;
  for (int i = 0; i < NBTN; i++) {
    if (rectHit(s_btn[i].r, mx, my, TOUCH_HIT_SLOP)) { hit = i; break; }
  }
  if (hit >= 0) {
    s_btn[hit].hits++;
    snprintf(s_lastBtn, sizeof s_lastBtn, "%s", s_btn[hit].label);
    if (hit != BTN_PAD) {
      s_btn[hit].pressed = true;
      drawBtn(s_btn[hit]);
    }
  }
  Serial.printf("DOWN map=(%d,%d) btn=%s lat=%luus\n",
                mx, my, hit >= 0 ? s_btn[hit].label : "-",
                (unsigned long)s_latUs);
}

static void onUp(int16_t mx, int16_t my) {
  uint32_t heldMs = millis() - s_dnMs;
  int16_t dx = mx - s_dnX, dy = my - s_dnY;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  bool tap = heldMs < TAP_MS && dx < TAP_MOVE_PX && dy < TAP_MOVE_PX;
  if (tap) s_taps++;

  for (int i = 0; i < NBTN; i++) {
    if (!s_btn[i].pressed) continue;
    s_btn[i].pressed = false;
    if (i == BTN_LOAD) { s_loadOn = !s_loadOn; }
    drawBtn(s_btn[i]);
    if (i == BTN_RST) resetStats();
  }
  eraseCross();
  Serial.printf("UP   map=(%d,%d) held=%lums%s\n", mx, my,
                (unsigned long)heldMs, tap ? " TAP" : "");
}

/* ==========================================================================
 * setup / loop — core 0
 * ========================================================================*/
void setup() {
  Serial.begin(115200);
  lcdBegin();
  paintChrome();
  Serial.println("\n[FABLE_DEV_TEST_SCREEN v1.1] display up");
}

void loop() {
  /* 1) drain touch events — NEVER skipped, even under load */
  uint32_t ev;
  while (rp2040.fifo.pop_nb(&ev)) {
    uint32_t type = ev >> 28;
    int16_t mx, my;
    mapTouch((ev >> 12) & 0xFFF, ev & 0xFFF, mx, my);
    TouchSnap sn; takeSnap(sn);
    if (type == EV_DOWN) onDown(mx, my, sn.tDownUs);
    else if (type == EV_UP) onUp(mx, my);
  }

  /* 2) live crosshair from latest sample */
  TouchSnap sn; takeSnap(sn);
  if (sn.down) {
    int16_t mx, my;
    mapTouch(sn.rawX, sn.rawY, mx, my);
    if (rectHit(PAD, mx, my, 0)) drawCross(mx, my);
  } else if (s_chX >= 0) {
    eraseCross();
  }

  /* 3) stats panel at ~20 Hz */
  static uint32_t lastStatMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastStatMs >= 50) {
    lastStatMs = nowMs;
    int16_t mx, my; mapTouch(sn.rawX, sn.rawY, mx, my);
    statsLine( 0, "RAW %3u,%3u", sn.rawX, sn.rawY);
    statsLine( 1, "MAP %3d,%3d", mx, my);
    statsLine( 2, "PTS %u MAX %u HELD %lums", sn.pts, g_maxPts,
               (unsigned long)sn.heldMs);
    statsLine( 3, "BTN %s LAT %luus", s_lastBtn, (unsigned long)s_latUs);
    statsLine( 4, "DN %lu UP %lu TAP %lu", (unsigned long)g_cntDown,
               (unsigned long)g_cntUp, (unsigned long)s_taps);
    statsLine( 5, "TP %luHZ UI %luHZ%s", (unsigned long)g_touchHz,
               (unsigned long)s_uiHz, s_loadOn ? " +LOAD!" : "");
    statsLine( 6, "INT %lu I2C ERR %lu", (unsigned long)g_intEdges,
               (unsigned long)g_i2cErr);
    statsLine( 7, "G: RNG %lu JMP %lu DRP %lu", (unsigned long)g_glRange,
               (unsigned long)g_glJump, (unsigned long)g_glDrop);
    statsLine( 8, "G: CNT %lu OVF %lu", (unsigned long)g_glCount,
               (unsigned long)g_glOvf);
    statsLine( 9, "CHIP %02X FW %02X VEND %02X %s", g_ftChip[0], g_ftChip[1],
               g_ftChip[2], g_ftOk ? "OK" : "INIT?");
    statsLine(10, "MODE POLL SLOP %d REL %d", TOUCH_HIT_SLOP, RELEASE_CONFIRM);
    statsLine(11, "UPTIME %lus", (unsigned long)(nowMs / 1000));
  }

  /* 4) UI frame-rate bookkeeping */
  s_frames++;
  static uint32_t uiWinMs = 0;
  if (nowMs - uiWinMs >= 1000) {
    s_uiHz = s_frames;
    s_frames = 0;
    uiWinMs = nowMs;
  }

  /* 5) serial commands */
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') resetStats();
  }

  /* 6) artificial future-feature load (LOAD button). This is the proof
     that core-0 slowness cannot break touch: with LOAD on, UI Hz craters
     but TP Hz stays pinned at TOUCH_HZ and every tap still lands. */
  if (s_loadOn) delay(LOAD_BLOCK_MS);
}
