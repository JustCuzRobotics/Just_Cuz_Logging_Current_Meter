/* ==========================================================================
 * ScreenDev.cpp — touch diagnostics.
 *
 * Shows where the controller thinks a press landed (the crosshair is the
 * ground truth for mapping — trust it over any description of where a tap
 * "should" have gone), the touch engine's counters, and the REAL enlarged hit
 * rects of the four controls that were historically hardest to hit. The panel
 * is one shared coordinate space across every screen, so those outlines line
 * up with the actual buttons on Live View and Graph.
 *
 * Like every other screen it paints chrome once and only inks what changed —
 * a screen whose job is measuring responsiveness must not be the thing making
 * the loop slow.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Config.h"
#include "Settings.h"
#include "Sampler.h"

extern JCR_FT6336 touch;

uint32_t gDevTaps = 0;
uint32_t gLoopUsAvg = 0;

struct DevTarget { const Target *t; uint16_t color; const char *label; int16_t lx, ly; };
static const DevTarget DEV_TARGETS[4] = {
  { &LIVE_T[LIVE_BTN_BACK],     COL_VOLT,    "LIVE BACK",  104,  16 },
  { &GRAPH_T[GRAPH_BTN_BACK],   COL_DANGER,  "GRAPH BACK", 104,  30 },
  { &GRAPH_T[GRAPH_BTN_CHIP_V], COL_TEXT,    "V CHIP",      60, 226 },
  { &GRAPH_T[GRAPH_BTN_CHIP_T], COL_TEXT_HI, "T CHIP",     134, 226 },
};

static void drawCross(int16_t x, int16_t y, uint16_t color) {
  tft.fillRect(x - 14, y - 1, 29, 3, color);
  tft.fillRect(x - 1, y - 14, 3, 29, color);
}

/* The theme toggle is duplicated here as well as in Settings because Dev
 * Mode is where the palette actually gets judged — every colour in the theme
 * is on this screen at once. Switching from here does not save; Settings is
 * where a choice is committed to flash. */
static void drawDevThemeBtn(bool pressed) {
  const JCRRect &r = DEV_T[DEV_BTN_THEME].vis;
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  char buf[24];
  snprintf(buf, sizeof buf, "THEME: %s",
           themeCurrent() == THEME_DARK ? "DARK" : "CLASSIC");
  t5(r.x + 8, r.y + (r.h - 8) / 2, buf, COL_TEXT_HI, fill);
}

/* Everything static. Split out so the crosshair can restore what it dragged
 * across: erasing its footprint with a background rect used to scrape
 * whatever chrome was underneath, which on a screen full of diagnostics
 * meant the diagnostics. */
static void paintDevChrome() {
  drawBackBtn(DEV_T[DEV_BTN_BACK].vis, false);
  drawDevThemeBtn(false);
  tRussoCentered(RUSSO16, tft.width() / 2, 5, "DEV MODE", COL_TEXT_HI, COL_BG);

  /* Faint outer outline = the real hit rect; solid inner = the drawn box. */
  for (uint8_t i = 0; i < 4; i++) {
    const JCRRect &h = DEV_TARGETS[i].t->hit;
    const JCRRect &v = DEV_TARGETS[i].t->vis;
    tft.drawRect(h.x, h.y, h.w, h.h, COL_BOX_BORDER);
    tft.drawRect(v.x, v.y, v.w, v.h, DEV_TARGETS[i].color);
    t5(DEV_TARGETS[i].lx, DEV_TARGETS[i].ly, DEV_TARGETS[i].label, DEV_TARGETS[i].color, COL_BG);
  }
  t5(152, 290, "TAP/DRAG ANYWHERE.", COL_TEXT_HI, COL_BG);
  t5(152, 302, "CROSSHAIR = GROUND TRUTH.", COL_TEXT_HI, COL_BG);
}

void paintDevOnce() {
  tft.fillScreen(COL_BG);
  paintDevChrome();
  updateDevTick(true);
}

void updateDevTick(bool forceClear) {
  static char    cache[12][40];
  static int16_t lastX = -100, lastY = -100;
  static bool    wasDown = false;
  if (forceClear) {
    for (uint8_t i = 0; i < 12; i++) cache[i][0] = 0;
    lastX = lastY = -100;
    wasDown = false;
  }

  JCRTouchPoint p;  touch.getTouch(p);
  JCRTouchStats s;  touch.getStats(s);

  /* Crosshair first, so the text pass below repaints anything it disturbed.
   * Erase the old footprint, restore the static chrome under it, and mark
   * every cached field dirty — the text pass then redraws them all, which is
   * a dozen short strings at Dev cadence and only while a finger moves. */
  if (wasDown && (!p.down || lastX != p.x || lastY != p.y)) {
    drawCross(lastX, lastY, COL_BG);
    paintDevChrome();
    for (uint8_t i = 0; i < 12; i++) cache[i][0] = 0;
  }

  char buf[40];
  const int16_t X = 200;
  field5(X,  64, 15, 1, COL_TEXT_HI, COL_BG, p.down ? "TOUCH: DOWN" : "TOUCH: UP", cache[0]);
  snprintf(buf, sizeof buf, "RAW    X:%4u Y:%4u", p.rawX, p.rawY);
  field5(X,  80, 23, 1, COL_TEXT_HI, COL_BG, buf, cache[1]);
  snprintf(buf, sizeof buf, "SCREEN X:%4d Y:%4d", p.x, p.y);
  field5(X,  96, 23, 1, COL_TEXT_HI, COL_BG, buf, cache[2]);

  const char *hit = "NONE";
  if (p.down)
    for (uint8_t i = 0; i < 4; i++)
      if (DEV_TARGETS[i].t->hit.contains(p.x, p.y)) { hit = DEV_TARGETS[i].label; break; }
  snprintf(buf, sizeof buf, "HIT: %s", hit);
  field5(X, 112, 19, 1, COL_TEXT_HI, COL_BG, buf, cache[3]);

  uint32_t uiHz = gLoopUsAvg ? (1000000UL / gLoopUsAvg) : 0;
  snprintf(buf, sizeof buf, "TP %luHZ  UI %luHZ", (unsigned long)s.sampleHz, (unsigned long)uiHz);
  field5(X, 128, 20, 1, COL_TEXT_HI, COL_BG, buf, cache[4]);
  snprintf(buf, sizeof buf, "DN %lu UP %lu TAP %lu",
           (unsigned long)s.downs, (unsigned long)s.ups, (unsigned long)gDevTaps);
  field5(X, 144, 26, 1, COL_TEXT_HI, COL_BG, buf, cache[5]);
  snprintf(buf, sizeof buf, "INT %lu  I2C ERR %lu",
           (unsigned long)s.intEdges, (unsigned long)s.i2cErrors);
  field5(X, 160, 24, 1, COL_TEXT_HI, COL_BG, buf, cache[6]);
  snprintf(buf, sizeof buf, "RNG %lu JMP %lu DRP %lu OVF %lu",
           (unsigned long)s.rangeGlitches, (unsigned long)s.jumpGlitches,
           (unsigned long)s.dropouts, (unsigned long)s.overflows);
  field5(X, 176, 34, 1, COL_TEXT_HI, COL_BG, buf, cache[7]);

  /* Responsiveness harness — what to read when touch has faded. */
  snprintf(buf, sizeof buf, "LAT %lu/%luMS  FMAX %luMS",
           (unsigned long)(gLatUsAvg / 1000), (unsigned long)(gLatUsMax / 1000),
           (unsigned long)(gLoopUsMax / 1000));
  field5(X, 196, 30, 1, COL_AMP, COL_BG, buf, cache[8]);
  /* Drift per register: 00 / 86 / 88 / A4. All four together = resets. */
  snprintf(buf, sizeof buf, "DRIFT %lu/%lu/%lu/%lu SVC %luUS RI %lu",
           (unsigned long)s.driftByReg[0], (unsigned long)s.driftByReg[1],
           (unsigned long)s.driftByReg[2], (unsigned long)s.driftByReg[3],
           (unsigned long)s.serviceUsMax, (unsigned long)s.reinits);
  field5(X, 212, 34, 1, COL_AMP, COL_BG, buf, cache[9]);
  snprintf(buf, sizeof buf, "TICK OVR %lu MAX %luUS  UP %lus",
           (unsigned long)gTickOverruns, (unsigned long)gTickUsMax,
           (unsigned long)(millis() / 1000));
  field5(X, 228, 34, 1, COL_AMP, COL_BG, buf, cache[10]);
  /* The one-glance indicator: if service rate is off, nothing else matters. */
  bool stall = (s.sampleHz && s.sampleHz < 190);
  field5(X, 248, 22, 2, stall ? COL_DANGER : COL_VOLT, COL_BG,
         stall ? "TP STALL" : "TP OK", cache[11]);

  /* Draw the new crosshair last so it sits on top of the freshly drawn text. */
  if (p.down) { drawCross(p.x, p.y, COL_AMP); lastX = p.x; lastY = p.y; }
  wasDown = p.down;
}

void devSetPressed(int8_t id, bool pressed) {
  if (id == DEV_BTN_THEME) drawDevThemeBtn(pressed);
  else                     drawBackBtn(DEV_T[DEV_BTN_BACK].vis, pressed);
}

void devDispatch(int8_t id) {
  if (id == DEV_BTN_THEME) {
    gSet.theme = (uint8_t)((themeCurrent() + 1) % THEME_COUNT);
    themeApply(gSet.theme);
    paintScreen(SCR_DEV);
    return;
  }
  goTo(SCR_HOME);
}
