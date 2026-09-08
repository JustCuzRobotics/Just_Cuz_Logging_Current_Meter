/* ==========================================================================
 * ScreenTest.cpp — ESC signal generation.
 *
 * This is the board's namesake feature: it is an "ESC Test Bench Mainboard",
 * and until now it could only measure what something else was driving.
 *
 * Two halves, because there are two ways people use a bench like this. The
 * top is a manual set point for holding a throttle steady while watching the
 * numbers. The bottom is an auto-cycle that steps between two pulse widths on
 * a timer, so a run can be repeated identically and left unattended.
 *
 * The output is deliberately not tied to this screen — a cycle keeps running
 * while you watch Live View or the Graph, which is the entire point. What is
 * tied to this screen is the ability to change it, plus the amber strip along
 * the top of every screen saying that it is live.
 *
 * All of this is state display over EscOut, which owns the PWM. Nothing here
 * blocks, and the per-tick update only reprints fields that changed.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "EscOut.h"
#include "Settings.h"

/* Cycle values live in gSet so they persist with everything else; that also
 * means editing them here is editing the thing Save writes. */
static uint16_t *cycField(uint8_t i) {
  switch (i) {
    case 0:  return &gSet.cycleLoUs;
    case 1:  return &gSet.cycleHiUs;
    case 2:  return &gSet.cycleLoMs;
    default: return &gSet.cycleHiMs;
  }
}

static void clampCycle() {
  for (uint8_t i = 0; i < 2; i++) {
    uint16_t *v = cycField(i);
    if (*v < ESC_PULSE_MIN_US) *v = ESC_PULSE_MIN_US;
    if (*v > ESC_PULSE_MAX_US) *v = ESC_PULSE_MAX_US;
  }
  for (uint8_t i = 2; i < 4; i++) {
    uint16_t *v = cycField(i);
    if (*v < 200)   *v = 200;
    if (*v > 60000) *v = 60000;
  }
}

/* ---- chrome --------------------------------------------------------- */

/* The four coarse/fine step buttons. RUSSO22 renders "-50" at 56 px in a
 * 56 px box, so it is the 5x7 face at scale 2 (54 px) that actually fits. */
static const char *const PULSE_STEP_LABEL[4] = { "-50", "-10", "+10", "+50" };
static void drawPulseStepBtn(uint8_t id, bool pressed) {
  const JCRRect &r = TEST_T[id].vis;
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, COL_BOX_BORDER);
  tRussoCentered(RUSSO16, r.cx(), r.y + (r.h - RUSSO16.height) / 2,
                 PULSE_STEP_LABEL[id - TEST_P_M50], COL_TEXT_HI, fill);
}

static void drawArmBtn(bool pressed) {
  const JCRRect &r = TEST_T[TEST_ARM].vis;
  bool on = escArmed();
  /* Armed is a lit fill; pressing inverts it, so the press still reads even
   * while armed — the one state where you most want to know the tap landed. */
  uint16_t fill   = (on != pressed) ? COL_BOX_PRESSED : COL_BOX_FILL;
  uint16_t accent = on ? COL_AMP : COL_DISABLED_TEXT;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, accent);
  /* 5x7 at scale 2, not Russo: this label changes length between states and
   * has to fit both times, and 6*len*scale is a width you can check rather
   * than a proportional guess. */
  t5Centered(r, on ? "ESC ARMED" : "ESC OFF", accent, fill, 2);
}

static void drawCycleBtn(bool pressed) {
  const JCRRect &r = TEST_T[TEST_CYCLE].vis;
  bool on = escCycling();
  uint16_t fill   = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  uint16_t accent = on ? COL_DANGER : COL_BOX_BORDER;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, accent);
  tRussoCentered(RUSSO22, r.cx(), r.y + (r.h - RUSSO22.height) / 2 + 1,
                 on ? "STOP CYCLE" : "START CYCLE", on ? COL_DANGER : COL_TEXT_HI, fill);
}

void paintTestOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(TEST_T[TEST_BACK].vis, false);
  tRussoCentered(RUSSO16, TEST_TITLE_CX, 6, "TEST MODE", COL_TEXT_HI, COL_BG);
  drawArmBtn(false);

  /* manual */
  t5(12, TEST_Y_MANUAL_LBL, "MANUAL PULSE US", COL_TEXT_HI, COL_BG);
  for (uint8_t i = TEST_P_M50; i <= TEST_P_P50; i++) drawPulseStepBtn(i, false);

  t5(12, TEST_Y_PERIOD_LBL, "FRAME PERIOD US", COL_TEXT_HI, COL_BG);
  drawStepBtn(TEST_T[TEST_PER_M].vis, false, false);
  drawStepBtn(TEST_T[TEST_PER_P].vis, false, true);

  /* auto-cycle */
  tft.drawFastHLine(12, TEST_Y_DIVIDER, 456, COL_BOX_BORDER);
  t5(12,  TEST_Y_CYC_LBL,   TEST_CYC_LABEL[0], COL_TEXT_HI, COL_BG);
  t5(252, TEST_Y_CYC_LBL,   TEST_CYC_LABEL[1], COL_TEXT_HI, COL_BG);
  t5(12,  TEST_Y_DWELL_LBL, TEST_CYC_LABEL[2], COL_TEXT_HI, COL_BG);
  t5(252, TEST_Y_DWELL_LBL, TEST_CYC_LABEL[3], COL_TEXT_HI, COL_BG);
  for (uint8_t i = TEST_LO_M; i <= TEST_DHI_P; i++)
    drawStepBtn(TEST_T[i].vis, false, (i & 1) != 0);

  drawCycleBtn(false);
  updateTestTick(true);
}

void updateTestTick(bool forceClear) {
  static char cPulse[16], cPeriod[16], cCyc[4][16], cStatus[40];
  static bool lastArmed = false, lastCycling = false;
  if (forceClear) {
    cPulse[0] = cPeriod[0] = cStatus[0] = 0;
    for (uint8_t i = 0; i < 4; i++) cCyc[i][0] = 0;
    lastArmed = escArmed(); lastCycling = escCycling();
  }

  char buf[40];
  snprintf(buf, sizeof buf, "%u", (unsigned)escPulse());
  drawStepperBox(TEST_PULSE_BOX, buf, escArmed() ? COL_AMP : COL_TEXT, cPulse);
  snprintf(buf, sizeof buf, "%u", (unsigned)escPeriod());
  drawStepperBox(TEST_PERIOD_BOX, buf, COL_TEXT, cPeriod);

  for (uint8_t i = 0; i < 4; i++) {
    snprintf(buf, sizeof buf, "%u", (unsigned)*cycField(i));
    drawStepperBox(TEST_CYC_BOX[i], buf, COL_TEXT, cCyc[i]);
  }

  /* Status doubles as the cycle countdown, so it is the one field that moves
   * on its own; everything else only changes when a button is pressed. */
  if (escCycling())
    snprintf(buf, sizeof buf, "CYCLE %s  %lus", escCycleAtHigh() ? "HIGH" : "LOW ",
             (unsigned long)((escCycleRemainMs() + 999) / 1000));
  else
    snprintf(buf, sizeof buf, "%s", escArmed() ? "HOLDING SET POINT" : "OUTPUT DISARMED");
  field5(TEST_STATUS_BOX.x + 4, TEST_STATUS_BOX.y + 12, 26, 1,
         escArmed() ? COL_AMP : COL_TEXT_HI, COL_BG, buf, cStatus);

  if (escArmed() != lastArmed)     { lastArmed = escArmed();     drawArmBtn(false); }
  if (escCycling() != lastCycling) { lastCycling = escCycling(); drawCycleBtn(false); }
}

/* ---- interaction ----------------------------------------------------- */

void testSetPressed(int8_t id, bool pressed) {
  switch (id) {
    case TEST_BACK:  drawBackBtn(TEST_T[TEST_BACK].vis, pressed); break;
    case TEST_ARM:   drawArmBtn(pressed);   break;
    case TEST_CYCLE: drawCycleBtn(pressed); break;
    case TEST_P_M50:
    case TEST_P_M10:
    case TEST_P_P10:
    case TEST_P_P50: drawPulseStepBtn((uint8_t)id, pressed); break;
    default:         drawStepBtn(TEST_T[id].vis, pressed, (id & 1) != 0); break;
  }
}

void testDispatch(int8_t id) {
  switch (id) {
    case TEST_BACK:  goTo(SCR_HOME); return;
    case TEST_ARM:   escArm(!escArmed()); break;

    case TEST_P_M50: escSetPulse((uint16_t)(escPulse() - 50)); break;
    case TEST_P_M10: escSetPulse((uint16_t)(escPulse() - 10)); break;
    case TEST_P_P10: escSetPulse((uint16_t)(escPulse() + 10)); break;
    case TEST_P_P50: escSetPulse((uint16_t)(escPulse() + 50)); break;

    /* Period steps in whole milliseconds — the frame rate is a compatibility
     * choice, not something anyone tunes by the microsecond. */
    case TEST_PER_M: escSetPeriod((uint16_t)(escPeriod() - 1000)); break;
    case TEST_PER_P: escSetPeriod((uint16_t)(escPeriod() + 1000)); break;

    case TEST_LO_M:  gSet.cycleLoUs -= 10; break;
    case TEST_LO_P:  gSet.cycleLoUs += 10; break;
    case TEST_HI_M:  gSet.cycleHiUs -= 10; break;
    case TEST_HI_P:  gSet.cycleHiUs += 10; break;
    case TEST_DLO_M: gSet.cycleLoMs = (gSet.cycleLoMs > 700) ? (uint16_t)(gSet.cycleLoMs - 500) : 200; break;
    case TEST_DLO_P: gSet.cycleLoMs += 500; break;
    case TEST_DHI_M: gSet.cycleHiMs = (gSet.cycleHiMs > 700) ? (uint16_t)(gSet.cycleHiMs - 500) : 200; break;
    case TEST_DHI_P: gSet.cycleHiMs += 500; break;

    case TEST_CYCLE:
      if (escCycling()) escCycleStop();
      else              escCycleStart();
      break;
  }
  clampCycle();
  /* The frame period is a hardware compatibility choice, so it persists. The
   * live pulse deliberately does NOT — settingsSave() always stores idle, so
   * a board that boots with a motor attached never comes up under throttle. */
  gSet.escPeriodUs = escPeriod();
  updateTestTick();
}
