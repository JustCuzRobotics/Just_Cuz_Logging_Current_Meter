/* ==========================================================================
 * TouchReliabilityTest — prove the panel and the mapping on real hardware.
 *
 * Shows a live crosshair (the ground truth for touch mapping — trust it over
 * any description of where a tap "should" have landed), the raw and mapped
 * coordinates, buttons down to 16 px to probe small-target reliability, and
 * every diagnostic counter the driver keeps.
 *
 * Read the counters like this:
 *   TP HZ     should sit at the configured sample rate on every screen
 *   I2C ERR   wiring or pull-up trouble
 *   RNG/CNT   the controller reported something impossible
 *   DRP       a held contact briefly read as released and was bridged
 *   OVF       the event queue filled — core 0 is not draining often enough
 *
 * Serial 115200; 'r' resets the counters.
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

#define C_BG      0x0020
#define C_PANEL   0x2124
#define C_ACCENT  0x05FF
#define C_GREY    0x8410

JCR_ST7796 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);
JCR_FT6336 touch(Wire1, PIN_CTP_SDA, PIN_CTP_SCL, PIN_CTP_RST, PIN_CTP_INT);
JCR_Text   text(tft);

volatile bool gCoreReady = false;

/* Core 1 does nothing but touch here — the library's helper covers that. */
void setup1() {
  touch.begin();
  touch.setMapping(320, 480, ROTATION);
  JCRTouchCore1::attach(touch);
  while (!gCoreReady) {}
}
void loop1() { JCRTouchCore1::run(); }

/* JCRRect comes from the library, so it needs no special handling. Btn is
 * declared here in the sketch, so drawBtn() — which takes one — needs a
 * MANUAL forward declaration: the Arduino IDE inserts its auto-generated
 * prototypes above this point, before Btn exists. Declaring types in a
 * header (as the library does) avoids this; if you keep them in the .ino,
 * this is the workaround. */
struct Btn { JCRRect r; const char *label; uint16_t color; uint32_t hits; bool pressed; };
static void drawBtn(const Btn &b);

static JCRRect PAD = { 2, 24, 256, 292 };
static Btn  btns[] = {
  { {262, 196, 68, 44}, "A",     0x07E0, 0, false },
  { {336, 196, 68, 44}, "B",     0x07FF, 0, false },
  { {410, 196, 66, 44}, "LOAD",  0xFD20, 0, false },
  { {262, 250, 26, 22}, "S1",    0xFFE0, 0, false },
  { {296, 250, 26, 22}, "S2",    0xFFE0, 0, false },
  { {330, 250, 26, 22}, "S3",    0xFFE0, 0, false },
  { {364, 250, 26, 22}, "S4",    0xFFE0, 0, false },
  { {398, 253, 16, 16}, "T",     0xF800, 0, false },
  { {424, 246, 52, 30}, "RST",   0xFFFF, 0, false },
};
static const uint8_t NBTN = sizeof(btns) / sizeof(btns[0]);
#define BTN_LOAD 2
#define BTN_RST  8

static bool  gLoad = false;
static uint32_t gTaps = 0, gUiHz = 0;
static int16_t  gCrossX = -1, gCrossY = -1;

static void drawBtn(const Btn &b) {
  bool on = (&b == &btns[BTN_LOAD]) && gLoad;
  uint16_t fill = b.pressed ? C_ACCENT : (on ? 0xFD20 : C_PANEL);
  uint16_t fg   = (b.pressed || on) ? 0x0000 : b.color;
  tft.fillRect(b.r.x, b.r.y, b.r.w, b.r.h, fill);
  tft.drawRect(b.r.x, b.r.y, b.r.w, b.r.h, b.color);
  text.setScale(b.r.h >= 40 ? 2 : 1);
  text.drawCentered(b.r.x + b.r.w / 2, b.r.y + (b.r.h - text.height()) / 2, b.label, fg, fill);
  if (b.r.h >= 40) {
    char n[10]; snprintf(n, sizeof n, "%lu", (unsigned long)b.hits);
    text.setScale(1);
    text.draw(b.r.x + 3, b.r.y + b.r.h - 10, n, C_GREY, fill);
  }
}

static void eraseCross() {
  if (gCrossX < 0) return;
  tft.fillRect(gCrossX - 14, gCrossY - 1, 29, 3, 0x0000);
  tft.fillRect(gCrossX - 1, gCrossY - 14, 3, 29, 0x0000);
  gCrossX = -1;
}
static void drawCross(int16_t x, int16_t y) {
  if (x < PAD.x + 16) x = PAD.x + 16;
  if (x > PAD.x + PAD.w - 17) x = PAD.x + PAD.w - 17;
  if (y < PAD.y + 16) y = PAD.y + 16;
  if (y > PAD.y + PAD.h - 17) y = PAD.y + PAD.h - 17;
  if (x == gCrossX && y == gCrossY) return;
  eraseCross();
  tft.fillRect(x - 14, y - 1, 29, 3, 0x07E0);
  tft.fillRect(x - 1, y - 14, 3, 29, 0x07E0);
  tft.fillRect(x - 14, y, 29, 1, 0xFFFF);
  tft.fillRect(x, y - 14, 1, 29, 0xFFFF);
  gCrossX = x; gCrossY = y;
}

#define STAT_LINES 10
static char statCache[STAT_LINES][40];
static void statLine(uint8_t i, const char *fmt, ...) {
  char buf[40];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (i >= STAT_LINES || strcmp(buf, statCache[i]) == 0) return;
  strcpy(statCache[i], buf);
  int16_t y = 30 + i * 14;
  tft.fillRect(262, y, 216, 8, C_BG);
  text.setScale(1);
  text.draw(262, y, buf, 0xFFFF, C_BG);
}

static void paintChrome() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, tft.width(), 22, C_PANEL);
  text.setScale(2);
  text.draw(4, 4, "TOUCH RELIABILITY", C_ACCENT, C_PANEL);
  tft.fillRect(PAD.x, PAD.y, PAD.w, PAD.h, 0x0000);
  tft.drawRect(PAD.x, PAD.y, PAD.w, PAD.h, 0x39E7);
  text.setScale(1);
  text.draw(PAD.x + 6, PAD.y + 4, "CROSSHAIR = GROUND TRUTH", C_GREY, 0x0000);
  for (uint8_t i = 0; i < NBTN; i++) drawBtn(btns[i]);
  memset(statCache, 0, sizeof statCache);
}

static void resetAll() {
  touch.resetStats();
  gTaps = 0;
  for (uint8_t i = 0; i < NBTN; i++) btns[i].hits = 0;
  for (uint8_t i = 0; i < 3; i++) drawBtn(btns[i]);
  Serial.println("[stats reset]");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  tft.setSPIPins(PIN_SCK, PIN_MOSI, PIN_MISO);
  tft.begin(40000000UL, ROTATION);
  text.setFont(JCR_Font5x7);
  paintChrome();
  gCoreReady = true;
  Serial.println("JCR_TouchScreen reliability test");
}

void loop() {
  JCRTouchEvent ev;
  while (touch.popEvent(ev)) {
    if (ev.type == JCR_TOUCH_DOWN) {
      for (uint8_t i = 0; i < NBTN; i++) {
        if (btns[i].r.contains(ev.x, ev.y)) {
          btns[i].hits++; btns[i].pressed = true; drawBtn(btns[i]);
          break;
        }
      }
      Serial.printf("DOWN raw=(%u,%u) map=(%d,%d)\n", ev.rawX, ev.rawY, ev.x, ev.y);
    } else {
      gTaps++;
      for (uint8_t i = 0; i < NBTN; i++) {
        if (!btns[i].pressed) continue;
        btns[i].pressed = false;
        if (i == BTN_LOAD) gLoad = !gLoad;
        drawBtn(btns[i]);
        if (i == BTN_RST) resetAll();
      }
      eraseCross();
    }
  }

  JCRTouchPoint p; touch.getTouch(p);
  if (p.down && PAD.contains(p.x, p.y)) drawCross(p.x, p.y);
  else if (!p.down) eraseCross();

  static uint32_t frames = 0, win = 0, lastStat = 0;
  frames++;
  uint32_t now = millis();
  if (now - win >= 1000) { gUiHz = frames; frames = 0; win = now; }

  if (now - lastStat >= 50) {
    lastStat = now;
    JCRTouchStats s; touch.getStats(s);
    statLine(0, "RAW %3u,%3u", p.rawX, p.rawY);
    statLine(1, "MAP %3d,%3d  PTS %u", p.x, p.y, p.points);
    statLine(2, "TP %luHZ  UI %luHZ%s", (unsigned long)s.sampleHz,
             (unsigned long)gUiHz, gLoad ? " +LOAD" : "");
    statLine(3, "DN %lu UP %lu TAP %lu", (unsigned long)s.downs,
             (unsigned long)s.ups, (unsigned long)gTaps);
    statLine(4, "I2C ERR %lu  INT %lu", (unsigned long)s.i2cErrors,
             (unsigned long)s.intEdges);
    statLine(5, "RNG %lu JMP %lu", (unsigned long)s.rangeGlitches,
             (unsigned long)s.jumpGlitches);
    statLine(6, "CNT %lu DRP %lu OVF %lu", (unsigned long)s.countGlitches,
             (unsigned long)s.dropouts, (unsigned long)s.overflows);
    statLine(7, "CHIP %02X FW %02X VEND %02X %s", touch.chipId(), touch.firmwareId(),
             touch.vendorId(), touch.ok() ? "OK" : "INIT?");
    statLine(8, "PANEL %dx%d ROT %d", tft.width(), tft.height(), tft.rotation());
    statLine(9, "UPTIME %lus", (unsigned long)(now / 1000));
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') resetAll();
  }

  /* Hold LOAD to stall core 0 and watch TP HZ refuse to move. */
  if (gLoad) delay(50);
}
