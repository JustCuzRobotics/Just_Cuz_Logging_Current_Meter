/* ==========================================================================
 * ScreenTestManual.cpp — Test Mode, MANUAL tab.
 *
 *   ESC ONE DIRECTION|BIDIRECTIONAL (arrow icon)   CONTROL BUTTONS|SLIDER     RELEASE HOLD|DEAD-MAN
 *   big readout: set point in us, throttle %, direction for BIDI
 *   BUTTONS: -50 -10 +10 +50 (the 50s in the big-step lime) (hold to repeat)  or  SLIDER: tap-to-jump/drag
 *   IDLE
 *
 * The set point only reaches the pin while armed, past the 2 s arming hold,
 * and in MANUAL phase (a running cycle owns the pin). Every change goes
 * through escManualSet(), which clamps to 1000-2000 us.
 *
 * DEAD-MAN (slider only): lifting the finger returns the set point to idle at
 * once. So does losing the touch: the .ino releases a held drag when the
 * controller reports up, when a new press arrives without an UP (lost event),
 * when the press has left the screen, or when touch samples stop arriving
 * for 60 ms (a hung FT6336 never sends an UP at all).
 *
 * The slider repaints its interior only when the value changes, from the
 * 50 ms tick — a drag moves the pin immediately (escManualSet on every loop)
 * while the drawing follows at frame rate. That keeps THE CONTRACT: no full
 * repaint per tick, and the display never throttles the motor response.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "EscOut.h"
#include "Settings.h"

static bool sliderMode() { return gSet.ctrlStyle == CTRL_SLIDER; }
static bool sDragLive = false;   /* the current slider press began while live */
static bool bidi()       { return gSet.escType == ESC_TYPE_BIDI; }

static bool manualLive() {
  return escArmed() && !escHolding() && escPhase() == PH_MANUAL;
}

/* One explanation per refusal, so a tap never silently does nothing. */
static bool requireLive() {
  if (manualLive()) return true;
  if (!escArmed())            showToast("Press ARM first");
  else if (escHolding())      showToast("Arming - wait 2 s");
  else                        showToast("Cycle running - STOP it first");
  return false;
}

/* ---- slider geometry ------------------------------------------------- */
static int16_t usToX(uint16_t us) {
  int32_t span = TMM_TRACK.w - TMM_THUMB_W;
  return (int16_t)(TMM_TRACK.x + TMM_THUMB_W / 2 + ((int32_t)us - ESC_ABS_MIN_US) * span / 1000);
}
static uint16_t xToUs(int16_t x) {
  int32_t span = TMM_TRACK.w - TMM_THUMB_W;
  int32_t rel = x - (TMM_TRACK.x + TMM_THUMB_W / 2);
  if (rel < 0) rel = 0;
  if (rel > span) rel = span;
  int32_t us = ESC_ABS_MIN_US + (rel * 1000 + span / 2) / span;
  us = (us + 2) / 5 * 5;                 /* 5 us grid: steady under a finger */
  /* BIDI: a small detent at neutral so "stop" is easy to find by feel. */
  if (bidi() && us > 1485 && us < 1515) us = ESC_BIDI_IDLE_US;
  return (uint16_t)us;
}

static void paintSliderInterior(uint16_t us) {
  const JCRRect &t = TMM_TRACK;
  tft.fillRect(t.x, t.y, t.w, t.h, COL_BOX_FILL);
  int16_t idleX = usToX(escIdleUs());
  int16_t x = usToX(us);
  /* level bar from idle to the set point, a third of the track tall */
  int16_t bh = t.h / 3, by = t.y + (t.h - bh) / 2;
  if (x != idleX) {
    int16_t x0 = x < idleX ? x : idleX, x1 = x < idleX ? idleX : x;
    tft.fillRect(x0, by, x1 - x0, bh, manualLive() ? COL_AMP : COL_DISABLED_TEXT);
  }
  /* idle / neutral mark */
  tft.fillRect(idleX - 1, t.y + 4, 2, t.h - 8, COL_TEXT_HI);
  /* thumb */
  tft.fillRoundRect(x - TMM_THUMB_W / 2, t.y + 2, TMM_THUMB_W, t.h - 4, 4,
                    manualLive() ? COL_TEXT : COL_DISABLED_TEXT);
}

static void paintSliderFrame() {
  const JCRRect &r = TMM_T[TMM_SLIDER - TH_N].vis;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_BORDER);
}

/* ---- chrome ----------------------------------------------------------- */
static void drawToggles(int8_t pressedId) {
  drawEscTypeBtn(TMM_T[TMM_ESC - TH_N].vis, pressedId == TMM_ESC, bidi(), !escArmed());
  drawLabelBtn(TMM_T[TMM_CTRL - TH_N].vis, pressedId == TMM_CTRL,
               sliderMode() ? "SLIDER" : "BUTTONS", COL_BOX_BORDER);
  const JCRRect &rr = TMM_T[TMM_RELEASE - TH_N].vis;
  if (sliderMode())
    drawLabelBtn(rr, pressedId == TMM_RELEASE,
                 gSet.releaseMode == RELEASE_DEADMAN ? "DEAD-MAN" : "HOLD",
                 gSet.releaseMode == RELEASE_DEADMAN ? COL_AMP : COL_BOX_BORDER);
  else
    tft.fillRect(rr.x, rr.y, rr.w, rr.h, COL_BG);
}

static const char *const STEP_LABEL[4] = { "-50", "-10", "+10", "+50" };

void paintTestManualOnce() {
  tft.fillScreen(COL_BG);
  paintTestHeader(SCR_TEST_MANUAL);
  drawToggles(-1);
  if (sliderMode()) {
    paintSliderFrame();
    paintSliderInterior(escManualUs());
  } else {
    for (uint8_t i = 0; i < 4; i++)
      drawDeltaBtn(TMM_T[TMM_M50 - TH_N + i].vis, false, STEP_LABEL[i], i == 0 || i == 3);
  }
  drawLabelBtn(TMM_T[TMM_IDLE - TH_N].vis, false, bidi() ? "NEUTRAL 1500" : "IDLE 1000", COL_BOX_BORDER);
  updateTestManualTick(true);
}

void updateTestManualTick(bool forceClear) {
  static char cBig[8], cUnit[16];
  static uint16_t lastSliderUs = 0;
  static bool lastLive = false, lastArmed = false;
  if (forceClear) { cBig[0] = cUnit[0] = 0; lastSliderUs = 0xFFFF; lastLive = manualLive(); lastArmed = escArmed(); }

  updateTestHeaderTick(forceClear);

  /* Readout: the set point, and throttle as a percentage of the travel. */
  uint16_t us = escManualUs();
  char buf[40];
  snprintf(buf, sizeof buf, "%u", (unsigned)us);
  uint16_t col = manualLive() ? COL_AMP : COL_TEXT;
  fieldBig(TMM_READ_X, TMM_READ_Y, 4, col, COL_BG, buf, cBig);
  if (bidi()) {
    int32_t d = (int32_t)us - ESC_BIDI_IDLE_US;
    int32_t pct = (d < 0 ? -d : d) / 5;
    if (d == 0) snprintf(buf, sizeof buf, "US  NEUTRAL");
    else        snprintf(buf, sizeof buf, "US  %s %ld%%", d > 0 ? "FWD" : "REV", (long)pct);
  } else {
    snprintf(buf, sizeof buf, "US  %ld%%", (long)(((int32_t)us - ESC_UNI_IDLE_US) / 10));
  }
  field5(TMM_READ_X + 4 * bigCellW() + 10, TMM_READ_Y + 6, 15, 2, COL_TEXT_HI, COL_BG, buf, cUnit);

  if (sliderMode() && (us != lastSliderUs || manualLive() != lastLive)) {
    lastSliderUs = us;
    paintSliderInterior(us);
  }
  lastLive = manualLive();
  if (escArmed() != lastArmed) { lastArmed = escArmed(); drawToggles(-1); }  /* ESC toggle greys */

  /* Status line */
  if (escTesting())
    snprintf(buf, sizeof buf, "LOG TEST RUNNING - SEE LOG TEST TAB");
  else if (escCycling())
    snprintf(buf, sizeof buf, "CYCLE RUNNING - SEE CYCLE TAB");
  else if (!escArmed())
    snprintf(buf, sizeof buf, "DISARMED - PRESS ARM");
  else if (escHolding())
    snprintf(buf, sizeof buf, "ARMING AT %u US  %lus",
             (unsigned)escIdleUs(), (unsigned long)((escHoldRemainMs() + 999) / 1000));
  else
    snprintf(buf, sizeof buf, "ARMED  OUT %u US%s",
             (unsigned)gEscOutUs,
             (sliderMode() && gSet.releaseMode == RELEASE_DEADMAN) ? "  DEAD-MAN" : "");
  testStatusLine(buf, escArmed() ? COL_AMP : COL_TEXT_HI, forceClear);
}

/* ---- interaction ------------------------------------------------------ */
int8_t testManualHit(int16_t x, int16_t y) {
  int8_t h = testHeaderHit(x, y);
  if (h >= 0) return h;
  for (int8_t id = TMM_ESC; id < TMM_N; id++) {
    bool isStep = id >= TMM_M50 && id <= TMM_P50;
    if (sliderMode() ? isStep : (id == TMM_SLIDER || id == TMM_RELEASE)) continue;
    if (TMM_T[id - TH_N].hit.contains(x, y)) return id;
  }
  return -1;
}

void testManualSetPressed(int8_t id, bool pressed) {
  if (id < TH_N) { testHeaderSetPressed(SCR_TEST_MANUAL, id, pressed); return; }
  switch (id) {
    case TMM_ESC: case TMM_CTRL: case TMM_RELEASE: drawToggles(pressed ? id : -1); break;
    case TMM_M50: case TMM_M10: case TMM_P10: case TMM_P50:
      drawDeltaBtn(TMM_T[id - TH_N].vis, pressed, STEP_LABEL[id - TMM_M50],
                   id == TMM_M50 || id == TMM_P50);
      break;
    case TMM_IDLE:
      drawLabelBtn(TMM_T[id - TH_N].vis, pressed, bidi() ? "NEUTRAL 1500" : "IDLE 1000", COL_BOX_BORDER);
      break;
    default: break;                       /* slider: no press visual        */
  }
}

bool testManualIsDrag(int8_t id) { return id == TMM_SLIDER && sliderMode(); }

/* Drags only count if the press itself started while the output was live.
 * Otherwise a finger resting on the slider through the 2 s arming hold would
 * step the throttle to its position the instant the hold ended. */
void testManualDrag(int8_t id, int16_t x, int16_t y) {
  (void)y;
  if (id == TMM_SLIDER && sDragLive && manualLive()) escManualSet(xToUs(x));
}

void testManualRelease(int8_t id) {
  if (id != TMM_SLIDER) return;
  sDragLive = false;
  if (gSet.releaseMode == RELEASE_DEADMAN) escManualIdle();
}

bool testManualRepeatable(int8_t id) {
  return id >= TMM_M50 && id <= TMM_P50 && !sliderMode() && manualLive();
}

/* Entering slider + dead-man must not leave a held throttle running with
 * nobody touching it: the set point drops to idle as the mode takes effect. */
static void deadmanGuard() {
  if (sliderMode() && gSet.releaseMode == RELEASE_DEADMAN) escManualIdle();
}

void testManualDispatch(int8_t id) {
  if (testHeaderDispatch(SCR_TEST_MANUAL, id)) return;
  switch (id) {
    case TMM_ESC:
      if (escArmed() || escTesting()) { showToast("Disarm to change ESC type"); return; }
      gSet.escType = bidi() ? ESC_TYPE_UNI : ESC_TYPE_BIDI;
      settingsFixProfileForType(gSet);
      escManualIdle();
      paintScreen(SCR_TEST_MANUAL);
      return;
    case TMM_CTRL:
      gSet.ctrlStyle = sliderMode() ? CTRL_STEP : CTRL_SLIDER;
      deadmanGuard();
      paintScreen(SCR_TEST_MANUAL);
      return;
    case TMM_RELEASE:
      gSet.releaseMode = gSet.releaseMode == RELEASE_DEADMAN ? RELEASE_HOLD : RELEASE_DEADMAN;
      deadmanGuard();
      drawToggles(-1);
      updateTestManualTick();
      return;
    case TMM_M50: case TMM_M10: case TMM_P10: case TMM_P50: {
      if (!requireLive()) return;
      static const int16_t d[4] = { -50, -10, 10, 50 };
      int32_t v = (int32_t)escManualUs() + d[id - TMM_M50];
      escManualSet((uint16_t)(v < 0 ? 0 : v));
      break;
    }
    case TMM_SLIDER:
      sDragLive = false;
      if (!requireLive()) return;
      sDragLive = true;
      /* Tap-to-jump from the press position itself, so a quick tap whose
       * DOWN and UP arrive in one pass still lands; the drag hook takes over
       * while the finger stays down. */
      escManualSet(xToUs(gDownX));
      break;
    case TMM_IDLE:
      escManualIdle();
      break;
  }
  updateTestManualTick();
}
