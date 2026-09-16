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

  for (uint8_t i = 0; i < 5; i++) {
    int16_t y = SET_ROW_Y[i];
    tRusso(RUSSO16, 18, y + 2, SET_ROW_LABEL[i], COL_TEXT, COL_BG);
    if (i != 1) t5(18, y + 24, SET_ROW_SUB[i], COL_TEXT_HI, COL_BG);
    if (i < 4) tft.drawFastHLine(18, y + 46, 450, COL_BOX_BORDER);
  }

  drawThemeBtn(false);
  drawStepBtn(SET_T[SET_FILTER_M].vis, false, false);
  drawStepBtn(SET_T[SET_FILTER_P].vis, false, true);
  drawActionBtn(SET_T[SET_DUMP].vis, false, "DUMP");
  drawActionBtn(SET_T[SET_SAVE].vis, false, "SAVE");
  drawActionBtn(SET_T[SET_DEV].vis,  false, "DEV MODE");

  updateSettingsTick(true);
}

void updateSettingsTick(bool forceClear) {
  static char cFilter[16], cSub[40];
  if (forceClear) { cFilter[0] = 0; cSub[0] = 0; }
  char buf[40];
  filterValue(buf, sizeof buf);
  drawStepperBox(SET_FILTER_BOX, buf, COL_TEXT, cFilter);
  filterSub(buf, sizeof buf);
  field5(18, SET_ROW_Y[1] + 24, 34, 1, COL_TEXT_HI, COL_BG, buf, cSub);
}

/* ---- interaction ----------------------------------------------------- */

void settingsSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case SET_BACK:     drawBackBtn(SET_T[SET_BACK].vis, pressed); break;
    case SET_THEME:    drawThemeBtn(pressed); break;
    case SET_FILTER_M: drawStepBtn(SET_T[id].vis, pressed, false); break;
    case SET_FILTER_P: drawStepBtn(SET_T[id].vis, pressed, true);  break;
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
