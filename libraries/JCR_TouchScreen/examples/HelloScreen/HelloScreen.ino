/* ==========================================================================
 * HelloScreen — the smallest useful JCR_TouchScreen sketch.
 *
 * Brings up the panel, draws some shapes and text, and reports touches over
 * serial. Start here to prove your wiring before anything else.
 *
 * Wiring below is for a Waveshare RP2040-Zero and a 3.5" ST7796 + FT6336U
 * module; change the pin numbers to match your board.
 * ========================================================================*/
#include <JCR_TouchScreen.h>

#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_DC    6
#define PIN_LCD_RST   7
#define PIN_LCD_BL    8
#define PIN_SD_CS     9      /* park HIGH: shares the module's MISO line */
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13

#define ROTATION      1      /* 0/2 = 320x480 portrait, 1/3 = 480x320 landscape */

JCR_ST7796 tft(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);
JCR_FT6336 touch(Wire1, PIN_CTP_SDA, PIN_CTP_SCL, PIN_CTP_RST, PIN_CTP_INT);
JCR_Text   text(tft);

void setup() {
  Serial.begin(115200);

  /* Park the module's SD card select before anything else drives the bus. */
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  tft.setSPIPins(PIN_SCK, PIN_MOSI, PIN_MISO);
  if (!tft.begin(40000000UL, ROTATION)) {
    Serial.println("display begin() failed (out of memory)");
    while (true) delay(1000);
  }

  touch.begin();
  /* Touch panel's own portrait size, plus the display rotation. If the
   * crosshair mirrors your finger, add invertX/invertY here. */
  touch.setMapping(320, 480, ROTATION);

  tft.fillScreen(0x0000);
  text.setFont(JCR_Font5x7);

  text.setScale(2);
  text.drawCentered(tft.width() / 2, 16, "JCR TOUCHSCREEN", 0x07FF, 0x0000);
  text.setScale(1);
  char buf[40];
  snprintf(buf, sizeof buf, "%dx%d  ROTATION %d", tft.width(), tft.height(), ROTATION);
  text.drawCentered(tft.width() / 2, 40, buf, 0xFFFF, 0x0000);

  tft.fillRoundRect(20, 70, 120, 70, 8, 0x001F);
  tft.drawRoundRect(20, 70, 120, 70, 8, 0xFFFF);
  tft.fillCircle(200, 105, 34, 0xF800);
  tft.drawLine(250, 140, 320, 70, 0x07E0);

  text.drawCentered(tft.width() / 2, tft.height() - 20, "TOUCH THE SCREEN", 0xFFE0, 0x0000);
  Serial.println("ready");
}

void loop() {
  /* Nothing else needs core 1 here, so touch is serviced from the main loop.
   * That is fine as long as loop() stays fast — see DualCoreSampling for the
   * arrangement that stays reliable no matter how slow your drawing gets. */
  touch.service();

  JCRTouchEvent ev;
  while (touch.popEvent(ev)) {
    if (ev.type == JCR_TOUCH_DOWN) {
      Serial.printf("DOWN  x=%d y=%d\n", ev.x, ev.y);
      tft.fillCircle(ev.x, ev.y, 6, 0xFFE0);
    } else {
      Serial.printf("UP    x=%d y=%d\n", ev.x, ev.y);
    }
  }
}
