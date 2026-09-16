/* ==========================================================================
 * logging_current_meter_FABLE.ino — v3.2 (2026-09-16)
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
 *   Settings.*    user settings and their flash persistence
 *   EscOut.*      ESC servo-signal output (SDK PWM, arm hold, auto-cycle)
 *   Logger.*      SD CSV logging, triggers, USB CSV stream
 *   Version.h     the version string shared by banner and log headers
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
 *   v3.2  2026-09-16  ESC OUTPUT WAS WRONG SINCE v3.1. arduino-pico clamps
 *                     analogWriteFreq() to >= 100 Hz without saying so, so
 *                     the 50 Hz frame ran at 100 Hz on a 20000-count range
 *                     and every pulse came out at half width (1000 us idle
 *                     was 500 us). The ESC pin now drives its PWM slice
 *                     through the Pico SDK at exactly 1 us per count, and
 *                     arming always emits idle for 2 s before anything else
 *                     (the old 'e' key armed at 1500 us). New: SD logging
 *                     (LOG screen — manual, Test-cycle, or current-threshold
 *                     start with a 1-15 min duration and 0.5 s pre-trigger;
 *                     raw + filtered V/I, energy since log start) and a USB
 *                     CSV stream of the same rows without energy. All
 *                     non-data serial output is now prefixed '#'. Settings
 *                     blob v2, migrating a v3.1c save.
 *   v3.1c 2026-09-08  First harness data: the FT6336U's mode register (0xA4)
 *                     was found drifting 47 times in 39 min and being
 *                     rewritten by the watchdog — the likely cause of the
 *                     fade, and the reason it "works a lot better" now. The
 *                     watchdog now counts drift per register (to tell a
 *                     resetting part from a flaky register) and checks every
 *                     500 ms. Navy classic palette (0x0020 was the green
 *                     LSB, not blue); Test Mode rows re-spaced; Dev Mode's
 *                     crosshair restores what it drags across.
 *   v3.1b 2026-09-08  DISPLAY WAS COLOUR-INVERTED SINCE v2.0. The IPS panel
 *                     needs INVON; the GFX firmware set it via its `ips`
 *                     flag, the JCR driver sent INVOFF. Every palette since
 *                     v2.0 was judged as its complement. Fixed in the
 *                     library (ips flag, default on) + LCD_IPS in Config.h.
 *                     Also a touch-responsiveness harness for chasing the
 *                     fade-after-a-minute report: tap latency, worst frame,
 *                     controller register watchdog + reinit, core-1 tick
 *                     health, a 1 s serial telemetry line, and serial keys
 *                     to bisect at the bench (see Serial below).
 *   v3.1a 2026-09-08  Test Mode / Settings UI fixes. The Russo One headers had
 *                     been generated with a reduced charset, so '+', '/', ','
 *                     and '%' were zero-width and silently dropped — the pulse
 *                     steppers read "-50 -10 -10 -50". Regenerated with those
 *                     glyphs (heights unchanged, so no layout drift). Three
 *                     labels that overflowed their boxes were measured and
 *                     re-fitted, stepper +/- became vector bars instead of a
 *                     5x7 speck, and PAL_DARK's borders went brighter, not
 *                     dimmer: with no box fill the strokes carry all the
 *                     structure, and dim ones left a flat wash.
 *   v3.1  2026-09-08  Test Mode (ESC signal, manual set point + auto-cycle on
 *                     hardware PWM), Settings screen, runtime themes with a
 *                     genuinely dark palette, and a tunable weighted moving
 *                     average on the V and I readings. The Calibrate screen
 *                     is gone — it only showed compile-time constants, so it
 *                     is now a serial dump from Settings.
 *   v3.0a 2026-09-08  Banner moved out of setup() into a first-connection
 *                     announce in loop(): USB CDC enumerates after setup()
 *                     begins, so the boot banner was being discarded and the
 *                     board looked dead over serial when it was running fine.
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
 * Serial 115200. Every line that is not a CSV data row starts with '#'.
 * Keys:
 *   r  reset touch diagnostic counters
 *   d  toggle the once-per-second [tp] telemetry line
 *   t  re-initialise the touch controller (runs on core 1)
 *   L  toggle synthetic core-0 load (full-screen fill every loop)
 *   f  filter 0 <-> 20 samples
 *   e  ESC output arm (idle, 2 s hold) / disarm
 *   p  print the ESC PWM slice registers (compare with a scope)
 *   s  USB CSV stream on/off (not saved - use the LOG screen's SAVE)
 *   g  SD log start/stop
 *   m  remount the SD card
 * ========================================================================*/
#include <JCR_TouchScreen.h>

#include "Config.h"
#include "Theme.h"
#include "Layout.h"
#include "Format.h"
#include "Sampler.h"
#include "Widgets.h"
#include "Screens.h"
#include "Settings.h"
#include "EscOut.h"
#include "Logger.h"
#include "Version.h"

JCR_ST7796 tft(PIN_LCD_CS, PIN_LCD_RS, PIN_LCD_RST, PIN_LCD_LED, LCD_IPS != 0);
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
    if ((int32_t)(micros() - next) > (int32_t)(TICK_US * 4)) {
      next = micros() + TICK_US;
      gTickOverruns++;
    }
    samplerTick(serviceTouch);      /* touch serviced between ADC channels */
  }
}

/* ============================ CORE 0 — the UI ============================ */
static void telemetryTick();
static void serialKeys();
static int8_t   gPressedId     = -1;
static ScreenId gPressedScreen = SCR_HOME;
static int16_t  gDownX = 0, gDownY = 0;
static uint32_t gDownMs = 0;

/* ---- responsiveness harness ----
 * Latency is micros() at dispatch minus the event's core-1 timestamp: the
 * time a registered press waited for core 0 to notice. It is the one number
 * that separates "taps are reacted to late" from "taps are not registered".
 * All of these are per telemetry window (1 s) and shown in Dev Mode. */
uint32_t gLatUsAvg = 0, gLatUsMax = 0, gLoopUsMax = 0;
static uint32_t sLatSum = 0, sLatN = 0;
static bool     sTelemetry = false;
static bool     sLoad = false;          /* synthetic core-0 load           */

/* Dispatch fires on the PRESS edge, which is what makes the UI feel
 * immediate. gPressedScreen guards the matching release: if the press
 * navigated away, the un-press repaint must not land on the new screen. */
static void pumpTouchEvents() {
  JCRTouchEvent ev;
  while (touch.popEvent(ev)) {
    if (ev.type == JCR_TOUCH_DOWN) {
      uint32_t lat = micros() - ev.atMicros;
      sLatSum += lat; sLatN++;
      if (lat > gLatUsMax) gLatUsMax = lat;
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

  /* Settings before anything draws — the palette and the filter window both
   * come out of it. escBegin() parks the ESC pin LOW without engaging the PWM
   * slice, so a reset mid-test never comes back under throttle. */
  settingsBegin();
  themeApply(gSet.theme);
  settingsApplyFilter();
  escBegin();

  tft.setSPIPins(PIN_SCK, PIN_MOSI, PIN_MISO);
  if (!tft.begin(SPI_HZ, LCD_ROTATION)) {
    /* Only fails if the scanline buffer could not be allocated — without it
     * every drawing call is a silent no-op, so say so rather than boot into
     * a blank panel. */
    Serial.println(F("# FATAL: display begin() failed (scanline buffer)"));
    while (true) delay(1000);
  }
  gfxText.setFont(JCR_Font5x7);

  /* Nothing is printed here on purpose — see the announce block in loop(). */

  goTo(SCR_HOME);
  gCoreReady = true;      /* release core 1 */

  /* The card mounts after the display (SdFat shares SPI0 and must not call
   * SPI.begin() itself) and after core 1 is released, because a mount with no
   * card can take ~2 s: touch and sampling run meanwhile, and the 6.7 s log
   * ring absorbs the samples. */
  logBegin();
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
    if (dtUs > gLoopUsMax) gLoopUsMax = dtUs;
  }
  lastLoopUs = nowUs;

  /* Announce on first serial connection, not from setup(). USB CDC only
   * enumerates after setup() has already started, so a banner printed there
   * goes to a port the host has not opened yet and is lost — which reads as
   * "the firmware isn't running" when it is. Announcing here also means
   * opening the monitor at any time reports which build is on the board,
   * without needing a reset. */
  static bool announced = false;
  if (!announced && Serial) {
    announced = true;
    /* Everything in this project's history so far was written on one day, so
     * a date alone does not identify a build. The compiler's own timestamp
     * does, and costs nothing. */
    Serial.println(F("# Logging Current Meter UI  " FW_VERSION));
    Serial.println(F("# built " __DATE__ " " __TIME__));
    Serial.println(F("# keys: r stats | d telemetry | t touch reinit | L load | f filter | e esc arm | p pwm | s stream | g log | m mount"));
    Serial.printf("# touch: chip 0x%02X fw 0x%02X vendor 0x%02X %s\n",
                  touch.chipId(), touch.firmwareId(), touch.vendorId(),
                  touch.ok() ? "ok" : "INIT FAILED");
    Serial.printf("# settings: %s  theme=%u  filter=%u samples (%ums)\n",
                  settingsLoadedFromFlash() ? "loaded from flash" : "defaults",
                  (unsigned)gSet.theme,
                  (unsigned)FILTER_SAMPLES[gSet.filterIndex],
                  (unsigned)filterWindowMs(gSet.filterIndex));
    Serial.printf("# log: %s  mode=%u rate=%s threshold=%uA duration=%umin  stream=%s\n",
                  logCardText(), (unsigned)gSet.logMode, LOG_RATE_LABEL[gSet.logRateIdx],
                  (unsigned)gSet.logThreshA, (unsigned)LOG_DUR_MIN[gSet.logDurIdx],
                  streamOn() ? "on" : "off");
    checkTargetOverlaps();
    if (streamOn()) streamPrintHeader();
  }

  pumpTouchEvents();      /* never skipped, however slow the frame */
  escTick();              /* auto-cycle state machine — never blocks       */
  escBarTick();           /* the armed strip, on whichever screen is up    */
  logTick();              /* drain samples: trigger, SD, USB stream        */
  logBarTick();           /* the recording strip along the bottom edge     */
  tickScreen(gScreen);
  updateToast();

  /* Synthetic load: ~60 ms of real SPI traffic per loop — the same pixel
   * count as a full-screen fill, which is worse than any frame the real UI
   * produces — but painted over the 4 px top strip sixty times so the screen
   * stays usable. If the fade reproduces faster with this on, it is core-0
   * sensitivity; if not, core-0 load is ruled out. */
  if (sLoad) {
    for (uint8_t i = 0; i < 60; i++) tft.fillRect(0, 0, tft.width(), ESC_BAR_H + 1, COL_GRID);
    escBarPaint();
  }

  telemetryTick();
  serialKeys();
}

/* ---- once-per-second telemetry --------------------------------------- */
static void telemetryTick() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 1000) return;
  lastMs = now;

  JCRTouchStats s;  touch.getStats(s);
  gLatUsAvg = sLatN ? (sLatSum / sLatN) : 0;

  /* availableForWrite() guard: a monitor that is attached but not draining
   * must never be able to stall core 0 — that would itself look exactly like
   * the fault being chased. */
  if (sTelemetry && Serial && Serial.availableForWrite() > 160) {
    uint32_t uiHz = gLoopUsAvg ? (1000000UL / gLoopUsAvg) : 0;
    Serial.printf("# [tp] t=%lus TP=%lu UI=%lu fmax=%lums lat=%lu/%lums dn=%lu up=%lu ovf=%lu "
                  "i2c=%lu drp=%lu jmp=%lu rng=%lu drift=%lu[%lu/%lu/%lu/%lu] svc=%luus "
                  "tick=%lu/%luus reinit=%lu esc=%u heap=%luk\n",
                  (unsigned long)(now / 1000), (unsigned long)s.sampleHz, (unsigned long)uiHz,
                  (unsigned long)(gLoopUsMax / 1000),
                  (unsigned long)(gLatUsAvg / 1000), (unsigned long)(gLatUsMax / 1000),
                  (unsigned long)s.downs, (unsigned long)s.ups, (unsigned long)s.overflows,
                  (unsigned long)s.i2cErrors, (unsigned long)s.dropouts,
                  (unsigned long)s.jumpGlitches, (unsigned long)s.rangeGlitches,
                  (unsigned long)s.regDrift,
                  (unsigned long)s.driftByReg[0], (unsigned long)s.driftByReg[1],
                  (unsigned long)s.driftByReg[2], (unsigned long)s.driftByReg[3],
                  (unsigned long)s.serviceUsMax,
                  (unsigned long)gTickOverruns, (unsigned long)gTickUsMax,
                  (unsigned long)s.reinits, (unsigned)escArmed(),
                  (unsigned long)(rp2040.getFreeHeap() / 1024));
  }

  /* Per-window maxima reset AFTER the line so Dev Mode and serial agree. */
  gLoopUsMax = 0; gLatUsMax = 0; sLatSum = 0; sLatN = 0;
  touch.resetServiceMax();
  gTickMaxResetReq = true;
}

/* ---- bench keys ------------------------------------------------------- */
static void serialKeys() {
  if (!Serial.available()) return;
  char c = (char)Serial.read();
  switch (c) {
    case 'r': case 'R':
      touch.resetStats(); gDevTaps = 0; gTickOverruns = 0;
      Serial.println(F("# [touch stats reset]"));
      break;
    case 'd': case 'D':
      sTelemetry = !sTelemetry;
      Serial.printf("# [telemetry %s]\n", sTelemetry ? "on" : "off");
      break;
    case 't': case 'T':
      touch.requestReinit();
      Serial.println(F("# [touch reinit requested - core 1 will restart the controller]"));
      break;
    case 'l': case 'L':
      sLoad = !sLoad;
      Serial.printf("# [synthetic core-0 load %s]\n", sLoad ? "ON" : "off");
      break;
    case 'f': case 'F':
      gSet.filterIndex = gSet.filterIndex ? 0 : (FILTER_OPTION_COUNT - 1);
      settingsApplyFilter();
      Serial.printf("# [filter %u samples]\n", (unsigned)gFilterSamples);
      break;
    case 'e': case 'E':
      /* Arms at idle with the 2 s hold, exactly like the ARM button. The old
       * key armed at 1500 us, which a unidirectional ESC refuses to arm on. */
      escArm(!escArmed());
      Serial.printf("# [esc %s]\n", escArmed() ? "ARMED - idle 1000us, 2s hold" : "off");
      escPrintPwm(Serial);
      break;
    case 'p': case 'P':
      escPrintPwm(Serial);
      break;
    case 's': case 'S':
      streamSet(!streamOn());
      if (!streamOn()) Serial.println(F("# [stream off]"));
      break;
    case 'g': case 'G':
      if (logRecording()) logStop("serial");
      else if (!logStart()) Serial.println(F("# [log] start failed - see card status"));
      break;
    case 'm': case 'M':
      if (logRecording()) Serial.println(F("# [log] stop the log before remounting"));
      else logMount();
      break;
    default: break;
  }
}
