/* ==========================================================================
 * ScreenGraph.cpp — 5 s scrolling plot, autoscaled, up to three channels.
 *
 * Rendering is column-diff: each of the 380 columns remembers the vertical
 * span it inked last frame, and only repaints when that span changes. Erasing
 * restores the gridline pixel where one ran, so the grid survives without
 * being redrawn. That is what keeps a 30 Hz plot inside the frame budget.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Sampler.h"

bool gGraphForceRedraw = false;

static bool    gShowV = true, gShowT = true;
static int16_t gCurCeilingCentiamps = 1000;
static int16_t gVoltBaseLo, gVoltBaseHi, gVoltLo, gVoltHi;
static int16_t gTempHi = 5000;

/* Previous-ink memory, one entry per column per channel. uint8_t is enough
 * only because the plot never extends below y=255 — check this if you move
 * the plot down the screen. */
static uint8_t gPrevY0[3][GRAPH_COLS], gPrevY1[3][GRAPH_COLS];

static const int16_t I_STEPS[] = { 2, 5, 10, 25, 50, 100, 150 };

static int16_t floorTo500(int32_t v) { int32_t q = v / 500; if (v < 0 && v % 500 != 0) q--; return (int16_t)(q * 500); }
static int16_t ceilTo500(int32_t v)  { int32_t q = v / 500; if (v > 0 && v % 500 != 0) q++; return (int16_t)(q * 500); }

/* Recomputed every frame, so the ceiling drops again once a spike scrolls
 * off the window. */
static int16_t niceCeilAmps(int16_t maxCentiamps) {
  int32_t target = (int32_t)maxCentiamps * 115 / 100;     /* 15% headroom */
  for (uint8_t i = 0; i < 7; i++)
    if ((int32_t)I_STEPS[i] * 100 >= target) return (int16_t)(I_STEPS[i] * 100);
  return I_STEPS[6] * 100;
}

/* The one float in the graphics path — a coordinate transform, not autoscale
 * maths, which stays integer above. */
static int16_t valueToY(int16_t value, int16_t lo, int16_t hi) {
  if (hi <= lo) return PLOT_Y1 - 1;
  float f = (float)(value - lo) / (float)(hi - lo);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  int16_t y = (int16_t)(PLOT_Y1 - 2 - (int16_t)(f * (PLOT_H - 3)));
  if (y < PLOT_Y0) y = PLOT_Y0;
  if (y > PLOT_Y1 - 2) y = PLOT_Y1 - 2;
  return y;
}

static bool isGridRow(int16_t y) {
  for (uint8_t r = 0; r < 6; r++)
    if (PLOT_Y0 + ((int32_t)r * PLOT_H) / 5 == y) return true;
  return false;
}

/* One windowed write for the whole span, restoring grid pixels as it goes. */
static void eraseSpan(int16_t x, int16_t y0, int16_t y1) {
  if (y0 < PLOT_Y0) y0 = PLOT_Y0;
  if (y1 > PLOT_Y1 - 1) y1 = PLOT_Y1 - 1;
  if (y1 < y0) return;
  static uint16_t col[PLOT_H + 2];
  int16_t n = (int16_t)(y1 - y0 + 1);
  for (int16_t i = 0; i < n; i++) col[i] = isGridRow((int16_t)(y0 + i)) ? COL_GRID : COL_BG;
  tft.drawVRun(x, y0, col, n);
}

static void drawGridFrame() {
  tft.drawRect(PLOT_X0 - 1, PLOT_Y0 - 1, PLOT_W + 2, PLOT_H + 2, COL_BOX_BORDER);
  for (uint8_t r = 0; r < 6; r++)
    tft.drawFastHLine(PLOT_X0, (int16_t)(PLOT_Y0 + ((int32_t)r * PLOT_H) / 5), PLOT_W, COL_GRID);
  for (uint8_t c = 0; c < 6; c++) {
    int16_t x = (int16_t)(PLOT_X0 + ((int32_t)c * PLOT_W) / 5);
    tft.drawFastVLine(x, PLOT_Y0, PLOT_H, COL_GRID);
    char lbl[2]; snprintf(lbl, sizeof lbl, "%d", 5 - c);
    t5(x - 2, PLOT_Y1 + 3, lbl, COL_TEXT_HI, COL_BG);
  }
  /* uint16_t counter: a uint8_t against 380 never terminates. */
  for (uint16_t i = 0; i < GRAPH_COLS; i++)
    gPrevY0[0][i] = gPrevY0[1][i] = gPrevY0[2][i] = 255;
}

static void drawAxisLabels(bool forceClear) {
  static char iCache[6][8] = { "", "", "", "", "", "" };
  static char vCache[6][8] = { "", "", "", "", "", "" };
  static char tCache[6][8] = { "", "", "", "", "", "" };
  if (forceClear)
    for (uint8_t i = 0; i < 6; i++) { iCache[i][0] = vCache[i][0] = tCache[i][0] = 0; }
  char buf[8];
  int16_t tX = gShowV ? 420 : 444;     /* T shares the margin when V is shown */
  for (uint8_t r = 0; r < 6; r++) {
    int16_t y = (int16_t)(PLOT_Y0 + ((int32_t)r * PLOT_H) / 5 - 4);
    int16_t iVal = (int16_t)(gCurCeilingCentiamps - (int32_t)r * gCurCeilingCentiamps / 5);
    fmtAxisAmps(iVal, buf, sizeof buf);
    field5(2, y, 5, 1, COL_AMP, COL_BG, buf, iCache[r]);
    if (gShowV) {
      fmtWhole((int16_t)(gVoltHi - (int32_t)r * (gVoltHi - gVoltLo) / 5), buf, sizeof buf);
      field5(444, y, 4, 1, COL_VOLT, COL_BG, buf, vCache[r]);
    }
    if (gShowT) {
      fmtWhole((int16_t)(gTempHi - (int32_t)r * (gTempHi - TEMP_LO_CENTIDEGC) / 5), buf, sizeof buf);
      field5(tX, y, 4, 1, COL_TEMP, COL_BG, buf, tCache[r]);
    }
  }
}

/* Repaint the plot and both label margins, and drop the ink memory. Used when
 * a channel is toggled: without this, hiding a trace would leave the old line
 * frozen on screen, since the render loop simply skips hidden channels. */
static void repaintPlot() {
  tft.fillRect(0, PLOT_Y0 - 8, tft.width(), PLOT_H + 16, COL_BG);
  drawGridFrame();
  gGraphForceRedraw = true;
}

void paintGraphOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtnCompact(GRAPH_T[GRAPH_BTN_BACK].vis, false);
  for (uint8_t i = 0; i < 4; i++) {
    const JCRRect &r = GRAPH_PRESENT[i];
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  }

  drawGridFrame();

  drawChip(GRAPH_CHIP_I, 'I', COL_AMP, true, 11);      /* static, always on */
  drawChip(GRAPH_T[GRAPH_BTN_CHIP_V].vis, 'V', COL_VOLT, gShowV, 6);
  drawChip(GRAPH_T[GRAPH_BTN_CHIP_T].vis, 'T', COL_TEMP, gShowT, 6);

  tft.fillRoundRect(GRAPH_TIMER_BOX.x, GRAPH_TIMER_BOX.y, GRAPH_TIMER_BOX.w, GRAPH_TIMER_BOX.h, 4, COL_BOX_FILL);
  tft.drawRoundRect(GRAPH_TIMER_BOX.x, GRAPH_TIMER_BOX.y, GRAPH_TIMER_BOX.w, GRAPH_TIMER_BOX.h, 4, COL_BOX_BORDER);
  drawIconBtn(GRAPH_T[GRAPH_BTN_TIMER_RST].vis, false, 'R');
  t5(330, 252, "V/T = RIGHT AXIS", COL_TEXT_HI, COL_BG);

  drawIconBtn(GRAPH_T[GRAPH_BTN_PEAK_RST].vis, false, 'R');
  for (uint8_t i = 0; i < 4; i++) {
    const JCRRect &r = GRAPH_PEAK[i];
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  }

  /* Seed the voltage window once per entry, biased toward sag: a pack won't
   * drop to 0 V, so a fixed 0-65 V axis wastes most of its range. It only
   * widens from here. */
  int16_t center = (int16_t)((((int32_t)gState.centivolts + 250) / 500) * 500);
  gVoltBaseLo = (int16_t)(center - 1000); if (gVoltBaseLo < 0) gVoltBaseLo = 0;
  gVoltBaseHi = (int16_t)(center + 500);
  gVoltLo = gVoltBaseLo; gVoltHi = gVoltBaseHi;
  gTempHi = 5000;

  updateGraphTick(true);
}

void updateGraphTick(bool forceClear) {
  static char presCache[4][10] = { "", "", "", "" };
  static char peakCache[4][10] = { "", "", "", "" };
  if (forceClear)
    for (uint8_t i = 0; i < 4; i++) { presCache[i][0] = 0; peakCache[i][0] = 0; }
  char buf[16], val[10];

  for (uint8_t i = 0; i < 4; i++) {
    switch (i) {
      case 0: fmt3SigCenti(gState.centiamps, val, sizeof val);  snprintf(buf, sizeof buf, "%s %sA", GRAPH_PRESENT_LABEL[i], val); break;
      case 1: fmt3SigWatts(gState.milliwatts, val, sizeof val); snprintf(buf, sizeof buf, "%s %sW", GRAPH_PRESENT_LABEL[i], val); break;
      case 2: fmt3SigCenti(gState.centivolts, val, sizeof val); snprintf(buf, sizeof buf, "%s %sV", GRAPH_PRESENT_LABEL[i], val); break;
      default: fmtDegC(gState.centidegc, val, sizeof val);      snprintf(buf, sizeof buf, "%s %s",  GRAPH_PRESENT_LABEL[i], val); break;
    }
    const JCRRect &r = GRAPH_PRESENT[i];
    static const uint16_t col[4] = { COL_AMP, COL_TEXT, COL_VOLT, COL_TEMP };
    field5(r.x + 6, r.y + (r.h - 16) / 2, 7, 2, col[i], COL_BOX_FILL, buf, presCache[i]);
  }

  for (uint8_t i = 0; i < 4; i++) {
    switch (i) {
      case 0: fmt3SigCenti(gState.peakCentiamps, val, sizeof val);         snprintf(buf, sizeof buf, "%s%sA", GRAPH_PEAK_LABEL[i], val); break;
      case 1: fmt3SigWatts(gState.peakMilliwatts, val, sizeof val);        snprintf(buf, sizeof buf, "%s%sW", GRAPH_PEAK_LABEL[i], val); break;
      case 2: fmt3SigCenti(gState.peakVAtPeakCentivolts, val, sizeof val); snprintf(buf, sizeof buf, "%s%sV", GRAPH_PEAK_LABEL[i], val); break;
      default: fmtDegC(gState.peakCentidegc, val, sizeof val);             snprintf(buf, sizeof buf, "%s%s",  GRAPH_PEAK_LABEL[i], val); break;
    }
    const JCRRect &r = GRAPH_PEAK[i];
    static const uint16_t col[4] = { COL_AMP, COL_TEXT, COL_VOLT, COL_TEMP };
    field5(r.x + 6, r.y + (r.h - 16) / 2, 7, 2, col[i], COL_BOX_FILL, buf, peakCache[i]);
  }

  fmtTimer(gState.runElapsedMs, buf, sizeof buf);
  field5(GRAPH_TIMER_BOX.x + 4, GRAPH_TIMER_BOX.y + 6, 10, 1, COL_TEXT, COL_BOX_FILL, buf, nullptr);

  /* ---- autoscale: integer min/max scan over the window ---- */
  int16_t maxI = 0, minV = 32767, maxV = -32768, maxT = TEMP_LO_CENTIDEGC;
  for (uint16_t c = 0; c < GRAPH_COLS; c++) {
    if (gRingI[c] > maxI) maxI = gRingI[c];
    if (gRingV[c] < minV) minV = gRingV[c];
    if (gRingV[c] > maxV) maxV = gRingV[c];
    if (gRingT[c] > maxT) maxT = gRingT[c];
  }
  gCurCeilingCentiamps = niceCeilAmps(maxI);
  int16_t candLo = floorTo500((int32_t)minV - 200);
  int16_t candHi = ceilTo500((int32_t)maxV + 200);
  gVoltLo = (candLo < gVoltBaseLo) ? candLo : gVoltBaseLo;
  gVoltHi = (candHi > gVoltBaseHi) ? candHi : gVoltBaseHi;
  int16_t candTHi = ceilTo500((int32_t)maxT + 300);
  gTempHi = (candTHi > 5000) ? candTHi : 5000;

  drawAxisLabels(forceClear);

  /* ---- plot: connect the dots, one vertical span per column ---- */
  struct Chan { const int16_t *ring; int16_t lo, hi; uint16_t color; bool show; };
  Chan chans[3] = {
    { gRingI, 0,                 gCurCeilingCentiamps, COL_AMP,  true   },
    { gRingV, gVoltLo,           gVoltHi,              COL_VOLT, gShowV },
    { gRingT, TEMP_LO_CENTIDEGC, gTempHi,              COL_TEMP, gShowT },
  };
  for (uint16_t c = 0; c < GRAPH_COLS; c++) {
    uint16_t idx  = (uint16_t)((gRingHead + 1 + c) % GRAPH_COLS);   /* oldest first */
    uint16_t prev = (uint16_t)((idx + GRAPH_COLS - 1) % GRAPH_COLS);
    int16_t  x    = (int16_t)(PLOT_X0 + c);
    for (uint8_t ch = 0; ch < 3; ch++) {
      if (!chans[ch].show) continue;
      int16_t yNow  = valueToY(chans[ch].ring[idx], chans[ch].lo, chans[ch].hi);
      int16_t yPrev = (c == 0) ? yNow : valueToY(chans[ch].ring[prev], chans[ch].lo, chans[ch].hi);
      int16_t y0 = (yNow < yPrev) ? yNow : yPrev;
      int16_t y1 = (int16_t)(((yNow > yPrev) ? yNow : yPrev) + 1);
      if (gPrevY0[ch][c] != 255 && (gPrevY0[ch][c] != y0 || gPrevY1[ch][c] != y1))
        eraseSpan(x, gPrevY0[ch][c], gPrevY1[ch][c]);
      tft.drawFastVLine(x, y0, (int16_t)(y1 - y0 + 1), chans[ch].color);
      gPrevY0[ch][c] = (uint8_t)y0;
      gPrevY1[ch][c] = (uint8_t)y1;
    }
  }
}

void graphSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case GRAPH_BTN_BACK:      drawBackBtnCompact(GRAPH_T[id].vis, pressed); break;
    case GRAPH_BTN_TIMER_RST:
    case GRAPH_BTN_PEAK_RST:  drawIconBtn(GRAPH_T[id].vis, pressed, 'R'); break;
    default: break;   /* the chips show state, not a press flash */
  }
}

void graphDispatch(int8_t id) {
  switch (id) {
    case GRAPH_BTN_BACK: goTo(SCR_HOME); break;
    case GRAPH_BTN_CHIP_V:
      gShowV = !gShowV;
      drawChip(GRAPH_T[id].vis, 'V', COL_VOLT, gShowV, 6);
      repaintPlot();
      break;
    case GRAPH_BTN_CHIP_T:
      gShowT = !gShowT;
      drawChip(GRAPH_T[id].vis, 'T', COL_TEMP, gShowT, 6);
      repaintPlot();
      break;
    case GRAPH_BTN_TIMER_RST:
      gCmdResetEnergyTimer = true; showToast("Energy + timer reset"); break;
    case GRAPH_BTN_PEAK_RST:
      gCmdResetPeaks = true; showToast("Peaks reset"); break;
  }
}
