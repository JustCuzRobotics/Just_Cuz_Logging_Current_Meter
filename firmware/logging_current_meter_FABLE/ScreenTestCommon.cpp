/* ==========================================================================
 * ScreenTestCommon.cpp — the header shared by Test Mode's three tabs.
 *
 *   [< HOME] [MANUAL][CYCLE][LOG TEST]                    [ARM] / [STOP]
 *   status line
 *
 * ARM arms at the ESC type's idle (1000 UNI / 1500 BIDI) with the 2 s hold.
 * STOP is the emergency control: it cuts the output immediately in every
 * state, from any tab. A Log Test stopped this way still finishes its 5 s
 * post-roll (unpowered) so the log captures the spin-down, then closes.
 *
 * The tab you leave is remembered in gSet.testTab (RAM; SAVE persists it), so
 * the Home tile returns you where you were.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "EscOut.h"
#include "Settings.h"

ScreenId testTabScreen(uint8_t tab) {
  return tab == 1 ? SCR_TEST_CYCLE : tab == 2 ? SCR_TEST_LOG : SCR_TEST_MANUAL;
}
static uint8_t tabOf(ScreenId s) {
  return s == SCR_TEST_CYCLE ? 1 : s == SCR_TEST_LOG ? 2 : 0;
}

/* ARM/STOP face: STOP (red, filled) whenever the output is live or a test is
 * still running its post-roll; ARM otherwise. */
static bool armShowsStop() { return escArmed() || escTesting(); }

static void drawArmBtn(bool pressed) {
  const JCRRect &r = TH_T[TH_ARM].vis;
  if (armShowsStop()) drawLabelBtn(r, pressed, "STOP", COL_DANGER, true);
  else                drawLabelBtn(r, pressed, "ARM", COL_AMP, false);
}

static void drawTab(uint8_t tab, bool active, bool pressed) {
  drawLabelBtn(TH_T[TH_TAB_MANUAL + tab].vis, pressed, TH_TAB_LABEL[tab],
               active ? COL_VOLT : COL_BOX_BORDER, active);
}

void paintTestHeader(ScreenId s) {
  drawBackBtnCompact(TH_T[TH_BACK].vis, false);
  t5(TH_T[TH_BACK].vis.x + 18, TH_T[TH_BACK].vis.y + 12, "HOME", COL_TEXT_HI, COL_BOX_FILL);
  for (uint8_t t = 0; t < 3; t++) drawTab(t, t == tabOf(s), false);
  drawArmBtn(false);
  updateTestHeaderTick(true);
}

void testHeaderSetPressed(ScreenId s, int8_t id, bool pressed) {
  switch (id) {
    case TH_BACK:
      drawBackBtnCompact(TH_T[TH_BACK].vis, pressed);
      t5(TH_T[TH_BACK].vis.x + 18, TH_T[TH_BACK].vis.y + 12, "HOME", COL_TEXT_HI,
         pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
      break;
    case TH_TAB_MANUAL: case TH_TAB_CYCLE: case TH_TAB_LOG:
      drawTab((uint8_t)(id - TH_TAB_MANUAL), (id - TH_TAB_MANUAL) == tabOf(s), pressed);
      break;
    case TH_ARM: drawArmBtn(pressed); break;
  }
}

int8_t testHeaderHit(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < TH_N; i++)
    if (TH_T[i].hit.contains(x, y)) return (int8_t)i;
  return -1;
}

bool testHeaderDispatch(ScreenId s, int8_t id) {
  switch (id) {
    case TH_BACK: goTo(SCR_HOME); return true;
    case TH_TAB_MANUAL: case TH_TAB_CYCLE: case TH_TAB_LOG: {
      uint8_t tab = (uint8_t)(id - TH_TAB_MANUAL);
      if (tab != tabOf(s)) { gSet.testTab = tab; goTo(testTabScreen(tab)); }
      return true;
    }
    case TH_ARM:
      if (armShowsStop()) escArm(false);     /* STOP: output off, now          */
      else                escArm(true);      /* ARM: idle + 2 s hold           */
      drawArmBtn(true);                      /* face changed under the finger  */
      return true;
  }
  return false;
}

void updateTestHeaderTick(bool forceClear) {
  static bool lastStop = false;
  if (forceClear) lastStop = armShowsStop();
  if (armShowsStop() != lastStop) { lastStop = armShowsStop(); drawArmBtn(false); }
}

void testStatusLine(const char *text, uint16_t color, bool forceClear) {
  static char cache[TM_STATUS_CHARS + 1];
  static uint16_t lastColor = 0;
  if (forceClear || color != lastColor) { cache[0] = 0; lastColor = color; }
  field5(TM_STATUS_X, TM_STATUS_Y, TM_STATUS_CHARS, 2, color, COL_BG, text, cache);
}
