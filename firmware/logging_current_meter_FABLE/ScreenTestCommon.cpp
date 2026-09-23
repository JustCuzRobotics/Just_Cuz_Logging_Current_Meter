/* ==========================================================================
 * ScreenTestCommon.cpp — the header shared by Test Mode's three tabs.
 *
 *   [< HOME] [MANUAL][CYCLE][LOG TEST]             [ARM] / [STOP] / [CUT]
 *   status line
 *
 * The right-hand button is the whole arm/stop control, and it escalates:
 *
 *   ARM      output off -> signal up at the ESC type's idle (1000 UNI /
 *            1500 BIDI) with the 2 s hold.
 *   STOP     something is moving (a run, or a manual throttle) -> idle now,
 *            signal still up. The motor stops at once and the ESC stays
 *            armed, so the next run does not wait out its start-up again.
 *   CUT      already sitting at idle with nothing running -> output off.
 *
 * Holding the button for a second does CUT — but only from a press that
 * started on STOP. The repeat machinery re-dispatches the button, so the
 * first press goes to neutral and the repeat a second later finds it at
 * neutral and cuts. A press that started on ARM never repeats (holding ARM
 * would otherwise arm the ESC and then cut it a second later, which reads as
 * ARM doing nothing), and repeats stop once the output is off, so a hold can
 * never run on into a re-arm either.
 *
 * A Log Test stopped either way still finishes its 5 s post-roll so the log
 * captures the spin-down — powered at neutral after a tap, unpowered after a
 * cut — then closes.
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

/* Which of the three the button is offering right now. */
enum ArmFace : uint8_t { FACE_ARM = 0, FACE_STOP, FACE_CUT };

static uint8_t armFace() {
  if (!escArmed()) return escTesting() ? FACE_STOP : FACE_ARM;  /* unpowered post-roll */
  return escAtNeutral() ? FACE_CUT : FACE_STOP;
}

/* What the button was offering when the finger went down. The repeat that
 * implements hold-to-cut is only allowed to escalate a press that started on
 * STOP — see the note at the top of this file. */
static uint8_t sPressFace = FACE_ARM;

static void drawArmBtn(bool pressed) {
  const JCRRect &r = TH_T[TH_ARM].vis;
  switch (armFace()) {
    case FACE_ARM:  drawLabelBtn(r, pressed, "ARM",  COL_AMP,    false); break;
    case FACE_CUT:  drawLabelBtn(r, pressed, "CUT",  COL_DANGER, false); break;
    default:        drawLabelBtn(r, pressed, "STOP", COL_DANGER, true);  break;
  }
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
    case TH_ARM:
      if (pressed) sPressFace = armFace();
      drawArmBtn(pressed);
      break;
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
      switch (armFace()) {
        case FACE_ARM:  escArm(true);  break;   /* signal up at idle, 2 s hold */
        case FACE_CUT:  escCut();      break;   /* already quiet: kill it      */
        default:        if (!escNeutral()) escCut(); break;  /* stop, stay live */
      }
      drawArmBtn(true);                      /* face changed under the finger  */
      return true;
  }
  return false;
}

/* Repeatable only while the output is live, so a held button escalates to CUT
 * exactly once and then stops — it can never wrap round to re-arming. */
bool testHeaderRepeatable(int8_t id) {
  return id == TH_ARM && escArmed() && sPressFace == FACE_STOP;
}

void updateTestHeaderTick(bool forceClear) {
  static uint8_t lastFace = 0xFF;
  if (forceClear) lastFace = armFace();
  if (armFace() != lastFace) { lastFace = armFace(); drawArmBtn(false); }
}

void testStatusLine(const char *text, uint16_t color, bool forceClear) {
  static char cache[TM_STATUS_CHARS + 1];
  static uint16_t lastColor = 0;
  if (forceClear || color != lastColor) { cache[0] = 0; lastColor = color; }
  field5(TM_STATUS_X, TM_STATUS_Y, TM_STATUS_CHARS, 2, color, COL_BG, text, cache);
}
