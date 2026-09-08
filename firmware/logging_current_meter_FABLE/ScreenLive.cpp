/* ScreenLive.cpp — V/I/T/W, energy, 2- and 3-minute marks, tare/peak reset. */
#include "Screens.h"
#include "Widgets.h"
#include "Sampler.h"

static const uint16_t STAT_COLOR[4] = { COL_VOLT, COL_AMP, COL_TEMP, COL_TEXT };

void paintLiveOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(LIVE_T[LIVE_BTN_BACK].vis, false);
  tRussoCentered(RUSSO16, tft.width() / 2, 5, "LIVE VIEW", COL_TEXT_HI, COL_BG);

  tft.fillRoundRect(LIVE_TIMER_BOX.x, LIVE_TIMER_BOX.y, LIVE_TIMER_BOX.w, LIVE_TIMER_BOX.h, 5, COL_BOX_FILL);
  tft.drawRoundRect(LIVE_TIMER_BOX.x, LIVE_TIMER_BOX.y, LIVE_TIMER_BOX.w, LIVE_TIMER_BOX.h, 5, COL_BOX_BORDER);
  drawIconBtn(LIVE_T[LIVE_BTN_TIMER_RST].vis, false, 'R');

  for (uint8_t i = 0; i < 4; i++) {
    const JCRRect &r = LIVE_STAT[i];
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_FILL);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_BORDER);
    t5(r.x + 8, r.y + 6, LIVE_STAT_LABEL[i], COL_TEXT_HI, COL_BOX_FILL);
  }

  t5(8, 122, "ENERGY", COL_TEXT_HI, COL_BG);
  drawIconBtn(LIVE_T[LIVE_BTN_ENERGY_RST].vis, false, 'R');
  drawBigValueChrome(LIVE_ENERGY[0]);
  drawBigValueChrome(LIVE_ENERGY[1]);

  t5(8, 184, "ESTIMATED ENERGY NEEDED", COL_TEXT_HI, COL_BG);
  drawMiniStatChrome(LIVE_EST[0], "2 MIN");
  drawMiniStatChrome(LIVE_EST[1], "3 MIN");

  drawActionBtn(LIVE_T[LIVE_BTN_TARE].vis, false, "TARE");
  drawActionBtn(LIVE_T[LIVE_BTN_PEAK_RST].vis, false, "PEAK RESET");

  updateLiveTick(true);
}

void updateLiveTick(bool forceClear) {
  static char statCache[4][8]    = { "", "", "", "" };
  static char energyCache[2][14] = { "", "" };
  static char estCache[2][20]    = { "", "" };
  if (forceClear) {
    for (uint8_t i = 0; i < 4; i++) statCache[i][0] = 0;
    for (uint8_t i = 0; i < 2; i++) { energyCache[i][0] = 0; estCache[i][0] = 0; }
  }
  char buf[24], val[10];

  fmt3SigCenti(gState.centivolts, val, sizeof val);
  fieldBig(LIVE_STAT[0].x + 4, LIVE_STAT[0].y + 38, 4, STAT_COLOR[0], COL_BOX_FILL, val, statCache[0]);
  fmt3SigCenti(gState.centiamps, val, sizeof val);
  fieldBig(LIVE_STAT[1].x + 4, LIVE_STAT[1].y + 38, 4, STAT_COLOR[1], COL_BOX_FILL, val, statCache[1]);
  fmtDegC(gState.centidegc, val, sizeof val);
  fieldBig(LIVE_STAT[2].x + 4, LIVE_STAT[2].y + 38, 4, STAT_COLOR[2], COL_BOX_FILL, val, statCache[2]);
  fmt3SigWatts(gState.milliwatts, val, sizeof val);
  fieldBig(LIVE_STAT[3].x + 4, LIVE_STAT[3].y + 38, 4, STAT_COLOR[3], COL_BOX_FILL, val, statCache[3]);

  snprintf(buf, sizeof buf, "%.2f", gState.energyWh);
  drawBigValueField(LIVE_ENERGY[0], COL_TEXT, buf, "WH", energyCache[0]);
  snprintf(buf, sizeof buf, "%.0f", gState.energyMah);
  drawBigValueField(LIVE_ENERGY[1], COL_TEXT, buf, "MA", energyCache[1]);

  fmtMark(gState.mark2Captured, gState.mark2Wh, gState.mark2Mah, buf, sizeof buf);
  drawMiniStatValue(LIVE_EST[0], COL_TEXT, buf, 2, estCache[0]);
  fmtMark(gState.mark3Captured, gState.mark3Wh, gState.mark3Mah, buf, sizeof buf);
  drawMiniStatValue(LIVE_EST[1], COL_TEXT, buf, 2, estCache[1]);

  /* No cache on the timer: the milliseconds change essentially every tick. */
  fmtTimer(gState.runElapsedMs, buf, sizeof buf);
  field5(LIVE_TIMER_BOX.x + 8, LIVE_TIMER_BOX.y + 11, 10, 1, COL_TEXT, COL_BOX_FILL, buf, nullptr);
}

void liveSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case LIVE_BTN_BACK:       drawBackBtn(LIVE_T[id].vis, pressed); break;
    case LIVE_BTN_TIMER_RST:
    case LIVE_BTN_ENERGY_RST: drawIconBtn(LIVE_T[id].vis, pressed, 'R'); break;
    case LIVE_BTN_TARE:       drawActionBtn(LIVE_T[id].vis, pressed, "TARE"); break;
    case LIVE_BTN_PEAK_RST:   drawActionBtn(LIVE_T[id].vis, pressed, "PEAK RESET"); break;
  }
}

void liveDispatch(int8_t id) {
  switch (id) {
    case LIVE_BTN_BACK: goTo(SCR_HOME); break;
    case LIVE_BTN_TIMER_RST:
    case LIVE_BTN_ENERGY_RST:
      gCmdResetEnergyTimer = true; showToast("Energy + timer reset"); break;
    case LIVE_BTN_TARE:
      gCmdTare = true; showToast("Tared - zero set"); break;
    case LIVE_BTN_PEAK_RST:
      gCmdResetPeaks = true; showToast("Peaks reset"); break;
  }
}
