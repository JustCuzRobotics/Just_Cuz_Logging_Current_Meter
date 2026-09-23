/* ==========================================================================
 * ScreenSettings.cpp — theme, reading filter, calibration dump, save, dev.
 *
 * The Calibrate screen used to live here as a whole screen. It only ever
 * displayed compile-time constants that cannot change at runtime, so it has
 * been demoted to the DUMP button below: the same rows, printed to serial
 * where the Arduino IDE can copy them, which is where they were going to end
 * up anyway. That reclaimed a Home tile for Test Mode.
 *
 * Changes take effect immediately in RAM. SAVE is what writes flash, and it
 * is explicit because an RP2040 flash commit halts BOTH cores for a few
 * milliseconds — fine when you choose the moment, not fine in the middle of
 * a capture.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Settings.h"
#include "Sampler.h"
#include "Config.h"
#include "Logger.h"
#include "Clock.h"

/* ---- the calibration dump (formerly ScreenCal.cpp) -------------------- */

void dumpCalibrationToSerial() {
  Serial.println(F("# ---- calibration constants ----"));
  Serial.printf("# V_GAIN_CAL      %.8f  %s\n", V_GAIN_CAL,      V_CAL_VALID  ? "FITTED"   : "NOMINAL");
  Serial.printf("# V_OFFSET_CAL    %.5f  %s\n", V_OFFSET_CAL,    V_CAL_VALID  ? "FITTED"   : "NOMINAL");
  Serial.printf("# I_QUIESCENT_CAL %.6f  %s\n", I_QUIESCENT_CAL, I_ZERO_VALID ? "MEASURED" : "NOMINAL");
  Serial.printf("# I_SENS_CAL      %.8f  %s\n", I_SENS_CAL,      I_GAIN_VALID ? "FITTED"   : "NOMINAL");
  Serial.printf("# RV1_OHMS        %.1f  MEASURED\n", RV1_OHMS);
  Serial.printf("# NTC_B           %.1f  FITTED\n",   NTC_B);
  Serial.printf("# NTC_R25         %.1f  ANCHORED\n", NTC_R25);
  Serial.println(F("# Recalibrate with display_bringup's serial v/i/n routines,"));
  Serial.println(F("# then paste the results into Config.h and reflash."));
}

/* ---- chrome ---------------------------------------------------------- */

/* The sample count goes in the stepper box; the window in ms goes on the
 * description line, because "20 / 263MS" is 140 px of RUSSO16 in a 120 px
 * box. Splitting them also puts the number people compare — the lag — next
 * to the sentence explaining what it costs. */
static void filterValue(char *out, size_t n) {
  uint8_t samples = FILTER_SAMPLES[gSet.filterIndex];
  if (!samples) snprintf(out, n, "OFF");
  else          snprintf(out, n, "%u", (unsigned)samples);
}
static void filterSub(char *out, size_t n) {
  uint16_t ms = filterWindowMs(gSet.filterIndex);
  if (!ms) snprintf(out, n, "V+I SMOOTHING - OFF, RAW READINGS");
  else     snprintf(out, n, "V+I SMOOTHING - %u MS WINDOW", (unsigned)ms);
}

/* Pre-roll in seconds with one decimal: "5.0 S" reads as a duration where a
 * bare 5000 would read as a pulse width, on a screen full of microseconds. */
static void preRollValue(char *out, size_t n) {
  uint16_t ms = gSet.preRollMs;
  if (!ms) snprintf(out, n, "OFF");
  else     snprintf(out, n, "%u.%u S", (unsigned)(ms / 1000), (unsigned)((ms % 1000) / 100));
}
static void preRollSub(char *out, size_t n) {
  /* 37 characters of 5x7 is what fits left of the button column. */
  if (!gSet.preRollMs) snprintf(out, n, "OFF - A RUN'S RAMP STARTS AT ONCE");
  else                 snprintf(out, n, "IDLE BEFORE A RUN - ESC STARTUP");
}
static bool preRollCanStep(bool up) {
  return up ? gSet.preRollMs + PRE_ROLL_STEP_MS <= PRE_ROLL_MAX_MS
            : gSet.preRollMs >= PRE_ROLL_STEP_MS;
}
static void drawPreRollBtn(int8_t id, bool pressed) {
  bool up = (id == SET_PRE_P);
  bool en = preRollCanStep(up);
  drawStepBtn(SET_T[id].vis, pressed && en, up, en);
}

static void drawThemeBtn(bool pressed) {
  const JCRRect &r = SET_T[SET_THEME].vis;
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, COL_BOX_BORDER);
  tRussoCentered(RUSSO16, r.cx(), r.y + (r.h - RUSSO16.height) / 2,
                 themeCurrent() == THEME_DARK ? "DARK" : "CLASSIC", COL_TEXT_HI, fill);
}

void paintSettingsOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(SET_T[SET_BACK].vis, false);
  tRussoCentered(RUSSO16, tft.width() / 2, 6, "SETTINGS", COL_TEXT_HI, COL_BG);

  for (uint8_t i = 0; i < SET_ROW_N; i++) {
    int16_t y = SET_ROW_Y[i];
    tRusso(RUSSO16, 18, y + 1, SET_ROW_LABEL[i], COL_TEXT, COL_BG);
    /* Rows 1 (filter) and 2 (clock) write their own line every tick. */
    if (i != 1 && i != 2) t5(18, y + 22, SET_ROW_SUB[i], COL_TEXT_HI, COL_BG);
    if (i < SET_ROW_N - 1) tft.drawFastHLine(18, y + 38, 450, COL_BOX_BORDER);
  }

  drawThemeBtn(false);
  drawStepBtn(SET_T[SET_FILTER_M].vis, false, false);
  drawStepBtn(SET_T[SET_FILTER_P].vis, false, true);
  drawActionBtn(SET_T[SET_CLOCK].vis, false, "SET CLOCK");
  drawPreRollBtn(SET_PRE_M, false);
  drawPreRollBtn(SET_PRE_P, false);
  drawActionBtn(SET_T[SET_DUMP].vis, false, "DUMP");
  drawActionBtn(SET_T[SET_SAVE].vis, false, "SAVE");
  drawActionBtn(SET_T[SET_DEV].vis,  false, "DEV MODE");

  updateSettingsTick(true);
}

/* "2026-09-23 14:05  MAY BE BEHIND" tells the operator both what the clock
 * says and how much to trust it, which is the whole reason the state exists.
 * The short form is what fits: 37 characters of 5x7 stops short of the
 * button column, and the long wording is on the SET CLOCK screen. */
static void clockSub(char *out, size_t n) {
  if (!clockUsable()) { snprintf(out, n, "NOT SET - LOGS HAVE NO DATE"); return; }
  char t[24];
  clockFormat(t, sizeof t, clockNow(), false);
  snprintf(out, n, "%s  %s", t, clockStateShort());
}

void updateSettingsTick(bool forceClear) {
  static char cFilter[16], cSub[44], cClock[44], cPre[16], cPreSub[48];
  if (forceClear) { cFilter[0] = 0; cSub[0] = 0; cClock[0] = 0; cPre[0] = 0; cPreSub[0] = 0; }
  char buf[44];
  filterValue(buf, sizeof buf);
  drawStepperBox(SET_FILTER_BOX, buf, COL_TEXT, cFilter);
  filterSub(buf, sizeof buf);
  field5(18, SET_ROW_Y[1] + 22, 37, 1, COL_TEXT_HI, COL_BG, buf, cSub);
  clockSub(buf, sizeof buf);
  field5(18, SET_ROW_Y[2] + 22, 37, 1, COL_TEXT_HI, COL_BG, buf, cClock);
  preRollValue(buf, sizeof buf);
  drawStepperBox(SET_PRE_BOX, buf, COL_TEXT, cPre);
  preRollSub(buf, sizeof buf);
  field5(18, SET_ROW_Y[3] + 22, 37, 1, COL_TEXT_HI, COL_BG, buf, cPreSub);
}

/* ---- interaction ----------------------------------------------------- */

void settingsSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case SET_BACK:     drawBackBtn(SET_T[SET_BACK].vis, pressed); break;
    case SET_THEME:    drawThemeBtn(pressed); break;
    case SET_FILTER_M: drawStepBtn(SET_T[id].vis, pressed, false); break;
    case SET_FILTER_P: drawStepBtn(SET_T[id].vis, pressed, true);  break;
    case SET_CLOCK:    drawActionBtn(SET_T[id].vis, pressed, "SET CLOCK"); break;
    case SET_PRE_M: case SET_PRE_P: drawPreRollBtn(id, pressed); break;
    case SET_DUMP:     drawActionBtn(SET_T[id].vis, pressed, "DUMP"); break;
    case SET_SAVE:     drawActionBtn(SET_T[id].vis, pressed, "SAVE"); break;
    case SET_DEV:      drawActionBtn(SET_T[id].vis, pressed, "DEV MODE"); break;
  }
}

/* Core 0 owns gFilterSamples; core 1 only ever reads it. Writing the sample
 * count last means core 1 can never see an index that has moved without the
 * value behind it. */
void settingsApplyFilter() { gFilterSamples = FILTER_SAMPLES[gSet.filterIndex]; }

void settingsDispatch(int8_t id) {
  switch (id) {
    case SET_BACK: goTo(SCR_HOME); return;

    case SET_THEME:
      gSet.theme = (uint8_t)((themeCurrent() + 1) % THEME_COUNT);
      themeApply(gSet.theme);
      paintScreen(SCR_SETTINGS);      /* repaint under the new palette */
      return;

    case SET_FILTER_M:
      if (gSet.filterIndex) gSet.filterIndex--;
      settingsApplyFilter();
      break;
    case SET_FILTER_P:
      if (gSet.filterIndex + 1 < FILTER_OPTION_COUNT) gSet.filterIndex++;
      settingsApplyFilter();
      break;

    case SET_CLOCK: goTo(SCR_CLOCK); return;

    case SET_PRE_M:
      if (preRollCanStep(false)) gSet.preRollMs -= PRE_ROLL_STEP_MS;
      break;
    case SET_PRE_P:
      if (preRollCanStep(true))  gSet.preRollMs += PRE_ROLL_STEP_MS;
      break;

    case SET_DUMP:
      dumpCalibrationToSerial();
      showToast("Calibration dumped to serial");
      return;

    case SET_SAVE:
      /* A flash commit pauses core 1 — never in the middle of a capture. */
      if (logRecording()) { showToast("Stop the log before saving"); return; }
      showToast(settingsSave() ? "Settings saved to flash" : "Save FAILED");
      return;

    case SET_DEV: goTo(SCR_DEV); return;
  }
  updateSettingsTick();
}
