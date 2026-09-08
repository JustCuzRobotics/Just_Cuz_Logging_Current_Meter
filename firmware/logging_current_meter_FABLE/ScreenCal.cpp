/* ==========================================================================
 * ScreenCal.cpp — read-only view of the baked calibration constants and
 * whether each has actually been fitted. Recalibrating still means running
 * display_bringup.ino's serial v/i/n routines and pasting the results into
 * Config.h, by design: this sketch doesn't duplicate that interactive menu.
 * Values can't change at runtime, so there is no per-tick update.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Config.h"

struct CalRow { const char *name; char val[16]; const char *tag; };
static CalRow gCalRows[7];

static void buildCalRows() {
  snprintf(gCalRows[0].val, sizeof gCalRows[0].val, "%.8f", V_GAIN_CAL);
  snprintf(gCalRows[1].val, sizeof gCalRows[1].val, "%.5f", V_OFFSET_CAL);
  snprintf(gCalRows[2].val, sizeof gCalRows[2].val, "%.6f", I_QUIESCENT_CAL);
  snprintf(gCalRows[3].val, sizeof gCalRows[3].val, "%.8f", I_SENS_CAL);
  snprintf(gCalRows[4].val, sizeof gCalRows[4].val, "%.1f", RV1_OHMS);
  snprintf(gCalRows[5].val, sizeof gCalRows[5].val, "%.1f", NTC_B);
  snprintf(gCalRows[6].val, sizeof gCalRows[6].val, "%.1f", NTC_R25);
  gCalRows[0].name = "V_GAIN_CAL";      gCalRows[0].tag = V_CAL_VALID  ? "FITTED"   : "NOMINAL";
  gCalRows[1].name = "V_OFFSET_CAL";    gCalRows[1].tag = V_CAL_VALID  ? "FITTED"   : "NOMINAL";
  gCalRows[2].name = "I_QUIESCENT_CAL"; gCalRows[2].tag = I_ZERO_VALID ? "MEASURED" : "NOMINAL";
  gCalRows[3].name = "I_SENS_CAL";      gCalRows[3].tag = I_GAIN_VALID ? "FITTED"   : "NOMINAL";
  gCalRows[4].name = "RV1_OHMS";        gCalRows[4].tag = "MEASURED";
  gCalRows[5].name = "NTC_B";           gCalRows[5].tag = "FITTED";
  gCalRows[6].name = "NTC_R25";         gCalRows[6].tag = "ANCHORED";
}

void paintCalOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(CAL_T[0].vis, false);
  tRussoCentered(RUSSO16, tft.width() / 2, 5, "CALIBRATE", COL_TEXT_HI, COL_BG);

  buildCalRows();
  for (uint8_t i = 0; i < 7; i++) {
    int16_t y = (int16_t)(44 + i * 32);
    tft.drawFastHLine(18, y + 30, 444, COL_BOX_BORDER);
    t5(18, y + 4,  gCalRows[i].name, COL_TEXT_HI, COL_BG);
    t5(18, y + 16, gCalRows[i].val,  COL_TEXT,    COL_BG);
    tft.drawRect(300, y + 14, 60, 12, COL_AMP);
    t5(304, y + 16, gCalRows[i].tag, COL_AMP, COL_BG);
  }

  t5(20, 286, "READ-ONLY. RECALIBRATE VIA DISPLAY_BRINGUP'S", COL_TEXT_HI, COL_BG);
  t5(20, 298, "SERIAL V/I/N, THEN PASTE INTO CONFIG.H + REFLASH.", COL_TEXT_HI, COL_BG);
}

void calSetPressed(int8_t id, bool pressed) { (void)id; drawBackBtn(CAL_T[0].vis, pressed); }
void calDispatch(int8_t id) { (void)id; goTo(SCR_HOME); }
