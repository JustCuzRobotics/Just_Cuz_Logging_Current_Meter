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

void paintDevOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(DEV_T[0].vis, false);
  tRussoCentered(RUSSO16, tft.width() / 2, 5, "DEV MODE", COL_TEXT_HI, COL_BG);

  /* Faint outer outline = the real hit rect; solid inner = the drawn box. */
  for (uint8_t i = 0; i < 4; i++) {
    const JCRRect &h = DEV_TARGETS[i].t->hit;
    const JCRRect &v = DEV_TARGETS[i].t->vis;
    tft.drawRect(h.x, h.y, h.w, h.h, COL_BOX_BORDER);
    tft.drawRect(v.x, v.y, v.w, v.h, DEV_TARGETS[i].color);
    t5(DEV_TARGETS[i].lx, DEV_TARGETS[i].ly, DEV_TARGETS[i].label, DEV_TARGETS[i].color, COL_BG);
  }
  t5(110, 300, "TAP/DRAG ANYWHERE. CROSSHAIR = GROUND TRUTH.", COL_TEXT_HI, COL_BG);
  updateDevTick(true);
}

void updateDevTick(bool forceClear) {
  static char    cache[8][40] = { "", "", "", "", "", "", "", "" };
  static int16_t lastX = -100, lastY = -100;
  static bool    wasDown = false;
  if (forceClear) {
    for (uint8_t i = 0; i < 8; i++) cache[i][0] = 0;
    lastX = lastY = -100;
    wasDown = false;
  }

  JCRTouchPoint p;  touch.getTouch(p);
  JCRTouchStats s;  touch.getStats(s);

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

  /* Erase only the crosshair's own last footprint. Dragging it across static
   * chrome scrapes a sliver — a contained cosmetic artefact on a debug
   * screen, cleared on re-entry. */
  if (wasDown && (!p.down || lastX != p.x || lastY != p.y)) drawCross(lastX, lastY, COL_BG);
  if (p.down) { drawCross(p.x, p.y, COL_AMP); lastX = p.x; lastY = p.y; }
  wasDown = p.down;
}

void devSetPressed(int8_t id, bool pressed) { (void)id; drawBackBtn(DEV_T[0].vis, pressed); }
void devDispatch(int8_t id) { (void)id; goTo(SCR_HOME); }
