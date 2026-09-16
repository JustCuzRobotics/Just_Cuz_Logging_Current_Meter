/* ==========================================================================
 * ScreenLog.cpp — SD logging and USB stream control.
 *
 * Everything here is state display and settings editing over Logger, which
 * owns the card, the trigger and the stream. The screen is optional by
 * design: in a test box the meter is set up once (mode, threshold, duration,
 * SAVE) and then left alone, so every setting on this page persists and the
 * bottom-edge strip tells you from any screen that a log is running (solid)
 * or armed (dashed).
 *
 * Changing mode, rate, threshold or duration is refused while a log is
 * recording — the file header documents the settings a log was taken with,
 * and a mid-file change would make that header wrong. SAVE is refused too:
 * a flash commit halts both cores, which would put a hole in the capture.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Settings.h"
#include "Logger.h"
#include "Format.h"

/* ---- values ----------------------------------------------------------- */

static void boxText(uint8_t i, char *out, size_t n) {
  switch (i) {
    case 0:
      snprintf(out, n, "%s", gSet.logMode == LOGMODE_CYCLE ? "CYCLE"
                           : gSet.logMode == LOGMODE_CURRENT ? "CURRENT" : "MANUAL");
      break;
    case 1: snprintf(out, n, "%s", LOG_RATE_LABEL[gSet.logRateIdx]); break;
    case 2: snprintf(out, n, "%u A", (unsigned)gSet.logThreshA); break;
    default: {
      uint8_t m = LOG_DUR_MIN[gSet.logDurIdx];
      if (m) snprintf(out, n, "%u MIN", (unsigned)m);
      else   snprintf(out, n, "NO LIMIT");
    }
  }
}

/* A stepper box whose face shrinks to RUSSO13 when the RUSSO16 string would
 * not clear the border — "CURRENT" and "NO LIMIT" are the tight ones. */
static void drawLogBox(uint8_t i, const char *text, uint16_t fg, char *cache) {
  if (cache && strcmp(cache, text) == 0) return;
  if (cache) { strncpy(cache, text, 15); cache[15] = 0; }
  const JCRRect &r = LOG_BOX[i];
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  gfxText.setFont(RUSSO16);
  const JCRFont &f = (gfxText.textWidth(text) <= r.w - 8) ? RUSSO16 : RUSSO13;
  tRussoCentered(f, r.cx(), r.y + (r.h - f.height) / 2, text, fg, COL_BOX_FILL);
}

/* ---- chrome ----------------------------------------------------------- */

static void drawUsbBtn(bool pressed) {
  const JCRRect &r = LOG_T[LOG_USB].vis;
  bool on = streamOn();
  uint16_t fill   = (on != pressed) ? COL_BOX_PRESSED : COL_BOX_FILL;
  uint16_t accent = on ? COL_VOLT : COL_BOX_BORDER;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, accent);
  t5Centered(r, on ? "USB ON" : "USB OFF", on ? COL_VOLT : COL_TEXT_HI, fill, 2);
}

static void drawSmallBtn(uint8_t id, bool pressed, const char *label) {
  const JCRRect &r = LOG_T[id].vis;
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, COL_BOX_BORDER);
  t5Centered(r, label, COL_TEXT_HI, fill, 2);
}

static void drawStartBtn(bool pressed) {
  const JCRRect &r = LOG_T[LOG_START].vis;
  bool rec = logRecording();
  uint16_t fill   = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  uint16_t accent = rec ? COL_DANGER : COL_BOX_BORDER;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, accent);
  tRussoCentered(RUSSO22, r.cx(), r.y + (r.h - RUSSO22.height) / 2 + 1,
                 rec ? "STOP LOG" : "START LOG", rec ? COL_DANGER : COL_TEXT_HI, fill);
}

void paintLogOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(LOG_T[LOG_BACK].vis, false);
  tRussoCentered(RUSSO16, LOG_TITLE_CX, 6, "LOG", COL_TEXT_HI, COL_BG);

  t5(12,  LOG_Y_ROW1_LBL, LOG_BOX_LABEL[0], COL_TEXT_HI, COL_BG);
  t5(252, LOG_Y_ROW1_LBL, LOG_BOX_LABEL[1], COL_TEXT_HI, COL_BG);
  t5(12,  LOG_Y_ROW2_LBL, LOG_BOX_LABEL[2], COL_TEXT_HI, COL_BG);
  t5(252, LOG_Y_ROW2_LBL, LOG_BOX_LABEL[3], COL_TEXT_HI, COL_BG);
  for (uint8_t i = LOG_MODE_M; i <= LOG_DUR_P; i++)
    drawStepBtn(LOG_T[i].vis, false, ((i - LOG_MODE_M) & 1) != 0);

  tft.drawFastHLine(12, LOG_Y_DIVIDER, 456, COL_BOX_BORDER);
  drawUsbBtn(false);
  drawSmallBtn(LOG_SAVE, false, "SAVE");
  drawSmallBtn(LOG_MOUNT, false, "REMOUNT SD");
  drawStartBtn(false);
  updateLogTick(true);
}

void updateLogTick(bool forceClear) {
  static char cBox[4][16], cState[20], cL2[40], cL3[40], cL4[40], cL5[40];
  static bool lastRec = false, lastUsb = false;
  if (forceClear) {
    for (uint8_t i = 0; i < 4; i++) cBox[i][0] = 0;
    cState[0] = cL2[0] = cL3[0] = cL4[0] = cL5[0] = 0;
    lastRec = logRecording(); lastUsb = streamOn();
  }

  char buf[48];
  bool cur = gSet.logMode == LOGMODE_CURRENT;
  /* The threshold box's colour depends on the mode, but its cache only keys
   * on text — so a mode change must invalidate it explicitly. */
  static bool lastCur = false;
  if (cur != lastCur) { lastCur = cur; cBox[2][0] = 0; }
  for (uint8_t i = 0; i < 4; i++) {
    boxText(i, buf, sizeof buf);
    /* The threshold only means something in CURRENT mode — dim it otherwise,
     * but keep it editable so it can be set up before switching. */
    drawLogBox(i, buf, (i == 2 && !cur) ? COL_DISABLED_TEXT : COL_TEXT, cBox[i]);
  }

  LogState st = logState();
  uint16_t stCol = st == LOGST_RECORDING ? COL_DANGER
                 : (st == LOGST_ERROR || st == LOGST_NO_CARD) ? COL_AMP
                 : st == LOGST_WAITING ? COL_VOLT : COL_TEXT;
  field5(LOG_STATUS_X, LOG_Y_STATE, 16, 2, stCol, COL_BG, logStateText(), cState);

  /* line 2: what the trigger / file is doing */
  if (st == LOGST_RECORDING)
    snprintf(buf, sizeof buf, "FILE %s", logFileName());
  else if (st == LOGST_WAITING)
    snprintf(buf, sizeof buf, "STARTS WHEN I > %u A", (unsigned)gSet.logThreshA);
  else if (st == LOGST_REARM)
    snprintf(buf, sizeof buf, "WAITING FOR I < %u A", (unsigned)gSet.logThreshA);
  else if (logFileName()[0])
    snprintf(buf, sizeof buf, "LAST %s", logFileName());
  else
    snprintf(buf, sizeof buf, "%s", gSet.logMode == LOGMODE_CYCLE ? "STARTS WITH TEST CYCLE" : "");
  field5(LOG_STATUS_X, LOG_Y_LINE2, LOG_STATUS_CHARS, 1, COL_TEXT_HI, COL_BG, buf, cL2);

  /* line 3: elapsed / duration */
  if (logFileName()[0]) {
    uint32_t s = logElapsedMs() / 1000;
    uint8_t durMin = LOG_DUR_MIN[gSet.logDurIdx];
    if (durMin && st == LOGST_RECORDING)
      snprintf(buf, sizeof buf, "TIME %02lu:%02lu / %02u:00", (unsigned long)(s / 60),
               (unsigned long)(s % 60), (unsigned)durMin);
    else
      snprintf(buf, sizeof buf, "TIME %02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  } else {
    buf[0] = 0;
  }
  field5(LOG_STATUS_X, LOG_Y_LINE2 + 14, LOG_STATUS_CHARS, 1, COL_TEXT_HI, COL_BG, buf, cL3);

  /* line 4: counts */
  if (logFileName()[0])
    snprintf(buf, sizeof buf, "ROWS %lu  DROP %lu", (unsigned long)logRows(), (unsigned long)logDrops());
  else
    buf[0] = 0;
  field5(LOG_STATUS_X, LOG_Y_LINE2 + 28, LOG_STATUS_CHARS, 1, COL_TEXT_HI, COL_BG, buf, cL4);

  /* line 5: card, plus USB drops when streaming */
  if (streamOn())
    snprintf(buf, sizeof buf, "%s  USB DROP %lu", logCardText(), (unsigned long)streamDrops());
  else
    snprintf(buf, sizeof buf, "%s", logCardText());
  field5(LOG_STATUS_X, LOG_Y_LINE2 + 42, LOG_STATUS_CHARS, 1,
         (st == LOGST_ERROR || st == LOGST_NO_CARD) ? COL_AMP : COL_TEXT_HI, COL_BG, buf, cL5);

  if (logRecording() != lastRec) { lastRec = logRecording(); drawStartBtn(false); }
  if (streamOn() != lastUsb)     { lastUsb = streamOn();     drawUsbBtn(false); }
}

/* ---- interaction ------------------------------------------------------ */

void logSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case LOG_BACK:  drawBackBtn(LOG_T[LOG_BACK].vis, pressed); break;
    case LOG_USB:   drawUsbBtn(pressed); break;
    case LOG_SAVE:  drawSmallBtn(LOG_SAVE, pressed, "SAVE"); break;
    case LOG_MOUNT: drawSmallBtn(LOG_MOUNT, pressed, "REMOUNT SD"); break;
    case LOG_START: drawStartBtn(pressed); break;
    default:
      if (id >= LOG_MODE_M && id <= LOG_DUR_P)
        drawStepBtn(LOG_T[id].vis, pressed, ((id - LOG_MODE_M) & 1) != 0);
      break;
  }
}

static void stepU8(uint8_t &v, int8_t d, uint8_t count) {
  int16_t n = (int16_t)v + d;
  if (n < 0) n = 0;
  if (n >= count) n = (int16_t)(count - 1);
  v = (uint8_t)n;
}

void logDispatch(int8_t id) {
  if (id == LOG_BACK) { goTo(SCR_HOME); return; }

  if (id >= LOG_MODE_M && id <= LOG_DUR_P && logRecording()) {
    showToast("Stop the log to change settings");
    return;
  }

  switch (id) {
    case LOG_MODE_M: stepU8(gSet.logMode, -1, LOGMODE_COUNT); logModeChanged(); break;
    case LOG_MODE_P: stepU8(gSet.logMode, +1, LOGMODE_COUNT); logModeChanged(); break;
    case LOG_RATE_M: stepU8(gSet.logRateIdx, -1, LOG_RATE_COUNT); break;
    case LOG_RATE_P: stepU8(gSet.logRateIdx, +1, LOG_RATE_COUNT); break;
    case LOG_THR_M:
      if (gSet.logThreshA > LOG_THRESH_MIN_A) gSet.logThreshA--;
      break;
    case LOG_THR_P:
      if (gSet.logThreshA < LOG_THRESH_MAX_A) gSet.logThreshA++;
      break;
    case LOG_DUR_M: stepU8(gSet.logDurIdx, -1, LOG_DUR_COUNT); break;
    case LOG_DUR_P: stepU8(gSet.logDurIdx, +1, LOG_DUR_COUNT); break;

    case LOG_USB:
      streamSet(!streamOn());
      gSet.streamOn = streamOn() ? 1 : 0;
      break;

    case LOG_SAVE:
      if (logRecording()) { showToast("Stop the log before saving"); return; }
      showToast(settingsSave() ? "Settings saved to flash" : "Save FAILED");
      return;

    case LOG_MOUNT:
      if (logRecording()) { showToast("Stop the log first"); return; }
      showToast("Mounting SD...");
      showToast(logMount() ? "SD mounted, read-back OK" : "SD mount FAILED");
      return;

    case LOG_START:
      if (logRecording()) logStop("manual");
      else if (!logStart()) showToast("Could not start - see card status");
      break;
  }
  updateLogTick();
}
