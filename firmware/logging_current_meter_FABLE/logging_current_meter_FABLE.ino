/* ==========================================================================
 * logging_current_meter_FABLE.ino — v3.0 (2026-09-08)
 *
 * Touchscreen UI for the Just 'Cuz Robotics Logging Current Meter (Rev A).
 * RP2040-Zero + 3.5" 480x320 ST7796/FT6336U on one FPC.
 *
 * This file is wiring only. Everything else lives in a module beside it:
 *
 *   Config.h      pins, bus rates, sampling cadence, calibration constants
 *   Theme.h       palette and fonts
 *   Layout.h      every pixel coordinate, and the touch targets
 *   Format.*      fixed-point value formatters
 *   Sampler.*     core-1 ADC, calibration maths, graph history ring
 *   Widgets.*     button chrome, cached fields, toast
 *   Screens.*     navigation and dispatch
 *   Screen*.cpp   one file per screen
 *
 * Display, touch and text come from the JCR_TouchScreen library.
 *
 * --------------------------------------------------------------------------
 * THE CONTRACT — the rules that keep touch reliable as this grows
 *
 *   CORE 1 is the instrumentation core. It owns the FT6336U and the ADC, and
 *   nothing else. It services touch at a fixed 200 Hz, and runs the 13.158 ms
 *   sampling tick with touch servicing interleaved BETWEEN the four channel
 *   bursts — so the ~5 ms of oversampling can never delay a touch sample by
 *   more than one channel (~1.3 ms).
 *
 *   CORE 0 owns the display and all UI logic. It drains queued touch events
 *   every pass and repaints incrementally.
 *
 *   1. Core 1 must never block, print or draw. Sampling and servicing only.
 *   2. Core 0 may be arbitrarily slow. A slow frame delays the REACTION to a
 *      tap, never its REGISTRATION — events queue, and overflow is counted.
 *   3. Never repaint the whole screen per tick. Chrome once on entry, then
 *      only the fields that changed. See the note atop Widgets.h for the
 *      forceClear rule that goes with cached fields.
 *   4. Core 0 never touches the ADC; core 1 never touches SPI or the display.
 *
 * --------------------------------------------------------------------------
 * Version history
 *   v3.0  2026-09-08  Split into modules; display/touch/text extracted into
 *                     the reusable JCR_TouchScreen library. Behaviour is
 *                     v2.1's; the touch event queue moved from the RP2040
 *                     hardware FIFO to the library's own ring buffer, which
 *                     frees the hardware FIFO for application use.
 *   v2.1  2026-09-08  Darker palette; V/T chips fixed (toggling repaints the
 *                     plot so a hidden trace disappears; on/off states made
 *                     visually distinct).
 *   v2.0  2026-09-08  Rebuild on the dual-core touch architecture proven in
 *                     FABLE_DEV_TEST_SCREEN v1.1; raw 40 MHz ST7796 driver,
 *                     Russo One typography, per-button enlarged hit rects.
 *
 * Serial 115200. 'r' resets the touch diagnostic counters.
 * ========================================================================*/
#include <JCR_TouchScreen.h>

#include "Config.h"
#include "Theme.h"
#include "Layout.h"
#include "Format.h"
#include "Sampler.h"
#include "Widgets.h"
#include "Screens.h"

JCR_ST7796 tft(PIN_LCD_CS, PIN_LCD_RS, PIN_LCD_RST, PIN_LCD_LED);
JCR_FT6336 touch(Wire1, PIN_CTP_SDA, PIN_CTP_SCL, PIN_CTP_RST, PIN_CTP_INT);
JCR_Text   gfxText(tft);

/* Core 0 sets this last, once the display is up; core 1 waits on it so the
 * two cores never race to initialise their buses. */
static volatile bool gCoreReady = false;

/* ======================= CORE 1 — touch and the ADC ======================= */
void setup1() {
  touch.begin(TOUCH_I2C_HZ, TOUCH_HZ);
  touch.setMapping(TOUCH_NATIVE_W, TOUCH_NATIVE_H, LCD_ROTATION,
                   TOUCH_INVERT_X, TOUCH_INVERT_Y);

  /* Nothing waits on CTP_INT — the controller runs in polling mode. The edge
   * count is purely diagnostic: it tells Dev Mode the interrupt line is alive
   * and roughly how much the panel thinks is happening. */
  attachInterrupt(digitalPinToInterrupt(PIN_CTP_INT),
                  []() { touch.noteInterrupt(); }, FALLING);

  while (!gCoreReady) { /* wait for core 0's display init */ }
}

static void serviceTouch() { touch.service(); }

void loop1() {
  touch.service();

  /* Sampling tick, with catch-up and a resync if we ever fall far behind. */
  static uint32_t next = 0;
  if (!next) next = micros();
  if ((int32_t)(micros() - next) >= 0) {
    next += TICK_US;
    if ((int32_t)(micros() - next) > (int32_t)(TICK_US * 4)) next = micros() + TICK_US;
    samplerTick(serviceTouch);      /* touch serviced between ADC channels */
  }
}

/* ============================ CORE 0 — the UI ============================ */
static int8_t   gPressedId     = -1;
static ScreenId gPressedScreen = SCR_HOME;
static int16_t  gDownX = 0, gDownY = 0;
static uint32_t gDownMs = 0;

/* Dispatch fires on the PRESS edge, which is what makes the UI feel
 * immediate. gPressedScreen guards the matching release: if the press
 * navigated away, the un-press repaint must not land on the new screen. */
static void pumpTouchEvents() {
  JCRTouchEvent ev;
  while (touch.popEvent(ev)) {
    if (ev.type == JCR_TOUCH_DOWN) {
      gDownX = ev.x; gDownY = ev.y; gDownMs = millis();
      int8_t id = hitTestScreen(gScreen, ev.x, ev.y);
      if (id >= 0) {
        gPressedId = id;
        gPressedScreen = gScreen;
        setPressedVisual(gScreen, id, true);
        dispatch(gScreen, id);
      }
    } else if (ev.type == JCR_TOUCH_UP) {
      int16_t dx = (int16_t)abs(ev.x - gDownX), dy = (int16_t)abs(ev.y - gDownY);
      if (millis() - gDownMs < 300 && dx < 12 && dy < 12) gDevTaps++;
      if (gPressedId >= 0) {
        if (gScreen == gPressedScreen) setPressedVisual(gScreen, gPressedId, false);
        gPressedId = -1;
      }
    }
  }
}

void setup() {
  /* SD_CS shares the FPC's MISO and must never float low — first GPIO. */
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  analogReadResolution(12);
  Serial.begin(115200);

  tft.setSPIPins(PIN_SCK, PIN_MOSI, PIN_MISO);
  if (!tft.begin(SPI_HZ, LCD_ROTATION)) {
    /* Only fails if the scanline buffer could not be allocated — without it
     * every drawing call is a silent no-op, so say so rather than boot into
     * a blank panel. */
    Serial.println(F("FATAL: display begin() failed (scanline buffer)"));
    while (true) delay(1000);
  }
  gfxText.setFont(JCR_Font5x7);

  Serial.println(F("Logging Current Meter UI  v3.0 (FABLE)"));
  checkTargetOverlaps();

  goTo(SCR_HOME);
  gCoreReady = true;      /* release core 1 last */
}

void loop() {
  /* Rolling average of one loop iteration, for Dev Mode's UI-rate readout.
   * Two micros() calls and a subtract — cheaper to always run than to gate. */
  static uint32_t lastLoopUs = 0;
  uint32_t nowUs = micros();
  if (lastLoopUs) {
    uint32_t dtUs = nowUs - lastLoopUs;
    gLoopUsAvg = gLoopUsAvg
        ? (uint32_t)(gLoopUsAvg + ((int32_t)dtUs - (int32_t)gLoopUsAvg) / 16)
        : dtUs;
  }
  lastLoopUs = nowUs;

  pumpTouchEvents();      /* never skipped, however slow the frame */
  tickScreen(gScreen);
  updateToast();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      touch.resetStats();
      gDevTaps = 0;
      Serial.println(F("[touch stats reset]"));
    }
  }
}
