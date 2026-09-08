/* ==========================================================================
 * DualCoreSampling — the arrangement that makes touch reliable under load.
 *
 * The point of this example: your app can own core 1 AND still get rock-solid
 * touch. Core 1 here runs a periodic "sampling" job (stand-in for an ADC
 * burst, a sensor read, whatever) and calls touch.service() between the
 * pieces of that job, so the touch sample cadence is never delayed by more
 * than one piece. Core 0 draws, and is deliberately made slow to prove the
 * point: hold BLOCK and core 0 stalls 50 ms per frame, yet the sample rate
 * does not move and no tap is lost.
 *
 * Rules that keep this working as you add features:
 *   1. Never block, print or draw on core 1 — sampling and service() only.
 *   2. Core 0 may be arbitrarily slow. A slow frame delays your REACTION to
 *      a tap, never its REGISTRATION: events queue and are drained later.
 *   3. Never repaint the whole screen every frame. Paint static chrome once,
 *      redraw only what changed.
 * ========================================================================*/
#include <JCR_TouchScreen.h>

#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_DC    6
#define PIN_LCD_RST   7
#define PIN_LCD_BL    8
#define PIN_SD_CS     9
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13
#define ROTATION      1

JCR_ST7796 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);
JCR_FT6336 touch(Wire1, PIN_CTP_SDA, PIN_CTP_SCL, PIN_CTP_RST, PIN_CTP_INT);
JCR_Text   text(tft);

volatile bool     gCoreReady = false;   /* core 0 releases core 1 when ready */
volatile uint32_t gSampleTicks = 0;     /* core 1 writes, core 0 reads       */
volatile int32_t  gLastSample  = 0;
static   bool     gBlock = false;       /* the artificial-load toggle        */

/* ---------------- core 1: sampling + touch, nothing else ---------------- */
void setup1() {
  touch.begin();                        /* core 1 owns the touch I2C bus */
  touch.setMapping(320, 480, ROTATION);
  while (!gCoreReady) { /* wait for core 0's display init */ }
}

static void samplingBurst() {
  /* Stand-in for four oversampled ADC channels. service() runs between the
   * pieces so a long burst can't starve the touch cadence. */
  int32_t acc = 0;
  for (uint8_t ch = 0; ch < 4; ch++) {
    for (uint16_t i = 0; i < 64; i++) { acc += (int32_t)analogRead(A0); delayMicroseconds(20); }
    touch.service();
  }
  gLastSample = acc / 256;
  gSampleTicks++;
}

void loop1() {
  touch.service();

  static uint32_t next = 0;
  if (!next) next = micros();
  if ((int32_t)(micros() - next) >= 0) {
    next += 13158UL;                                  /* ~76 Hz sample tick */
    if ((int32_t)(micros() - next) > 4 * 13158L) next = micros() + 13158UL;
    samplingBurst();
  }
}

/* ---------------------------- core 0: the UI ---------------------------- */
static const JCRRect BLOCK_BTN = { 300, 250, 160, 56 };

static void drawBlockButton() {
  uint16_t fill = gBlock ? 0xFD20 : 0x2124;
  tft.fillRoundRect(BLOCK_BTN.x, BLOCK_BTN.y, BLOCK_BTN.w, BLOCK_BTN.h, 8, fill);
  tft.drawRoundRect(BLOCK_BTN.x, BLOCK_BTN.y, BLOCK_BTN.w, BLOCK_BTN.h, 8, 0xFFFF);
  text.setScale(2);
  text.drawCentered(BLOCK_BTN.x + BLOCK_BTN.w / 2, BLOCK_BTN.y + 20,
                    gBlock ? "BLOCKING" : "BLOCK", 0x0000, fill);
}

/* Redraw a fixed-width field in place: the 5x7 font is monospace, so the old
 * text is erased simply by covering maxChars cells. */
static void field(int16_t x, int16_t y, uint8_t maxChars, const char *s, uint16_t fg) {
  tft.fillRect(x, y, (int16_t)maxChars * text.charWidth('0'), text.height(), 0x0000);
  text.draw(x, y, s, fg, 0x0000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  tft.setSPIPins(PIN_SCK, PIN_MOSI, PIN_MISO);
  tft.begin(40000000UL, ROTATION);
  text.setFont(JCR_Font5x7);

  tft.fillScreen(0x0000);
  text.setScale(2);
  text.drawCentered(tft.width() / 2, 10, "DUAL CORE TOUCH", 0x07FF, 0x0000);
  text.setScale(1);
  text.draw(20, 60,  "TOUCH SAMPLE RATE:", 0xFFFF, 0x0000);
  text.draw(20, 90,  "UI FRAME RATE:",     0xFFFF, 0x0000);
  text.draw(20, 120, "SENSOR TICKS:",      0xFFFF, 0x0000);
  text.draw(20, 150, "TAPS REGISTERED:",   0xFFFF, 0x0000);
  text.draw(20, 200, "HOLD BLOCK: UI STALLS, TOUCH DOES NOT.", 0x8410, 0x0000);
  drawBlockButton();

  gCoreReady = true;     /* release core 1 last */
}

void loop() {
  /* Drain touch events first, every pass — this is what must never be
   * skipped, however slow the rest of the frame gets. */
  static uint32_t taps = 0;
  JCRTouchEvent ev;
  while (touch.popEvent(ev)) {
    if (ev.type == JCR_TOUCH_DOWN) {
      taps++;
      if (BLOCK_BTN.contains(ev.x, ev.y)) { gBlock = !gBlock; drawBlockButton(); }
      else tft.fillCircle(ev.x, ev.y, 5, 0x07E0);
    }
  }

  static uint32_t frames = 0, windowMs = 0, uiHz = 0;
  frames++;
  uint32_t now = millis();
  if (now - windowMs >= 1000) { uiHz = frames; frames = 0; windowMs = now; }

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 100) {
    lastDraw = now;
    JCRTouchStats st; touch.getStats(st);
    char b[24];
    snprintf(b, sizeof b, "%lu HZ", (unsigned long)st.sampleHz);  field(240, 60,  8, b, 0x07E0);
    snprintf(b, sizeof b, "%lu HZ", (unsigned long)uiHz);         field(240, 90,  8, b, gBlock ? 0xF800 : 0x07E0);
    snprintf(b, sizeof b, "%lu", (unsigned long)gSampleTicks);    field(240, 120, 8, b, 0xFFFF);
    snprintf(b, sizeof b, "%lu (LOST %lu)", (unsigned long)taps,
             (unsigned long)st.overflows);                        field(240, 150, 16, b, 0xFFFF);
  }

  if (gBlock) delay(50);   /* the artificial load */
}
