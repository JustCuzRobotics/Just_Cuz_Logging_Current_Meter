/* ==========================================================================
 * ScreenTestCycle.cpp — Test Mode, CYCLE and LOG TEST tabs.
 *
 * Both tabs edit ONE saved profile (gSet.prof*):
 *
 *   [LOW US ][HIGH US][RAMP UP][DWELL HI]     tap a tile to select it
 *   [RAMP DN][DWELL LO][DIRECTION][CYCLES]
 *   [-BIG][-SMALL]   value   [+SMALL][+BIG]   hold 1 s to repeat, 5 per s
 *   status
 *   [RESET DEFAULTS]        [START ... / STOP ...]
 *
 * CYCLE runs RAMP UP -> DWELL HI -> RAMP DOWN -> DWELL LO until stopped.
 * LOG TEST opens LOG_<n>_TEST_<V>V.CSV, then 3 s pre-roll at idle (the 2 s
 * arming hold runs inside it), N cycles, 5 s post-roll, closes the log and
 * disarms. Aborting jumps to the post-roll so the spin-down is still logged.
 *
 * The profile is locked while a run is active: EscOut snapshots it at start,
 * and the log header records it, so an edit mid-run would change neither —
 * refusing it avoids the screen claiming a value the motor is not seeing.
 *
 * Tiles that do not apply are greyed and unselectable: LOW in BIDI (the low
 * end is neutral), DIRECTION in UNI, CYCLES on the CYCLE tab (continuous).
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "EscOut.h"
#include "Settings.h"
#include "Logger.h"

static uint8_t  sSel = TILE_HIGH;
static uint32_t sResetArmMs = 0;          /* first RESET tap, for the confirm */

static bool logTab()  { return gScreen == SCR_TEST_LOG; }
static bool bidi()    { return gSet.escType == ESC_TYPE_BIDI; }
static bool running() { return escCycling() || escTesting(); }

static bool tileEnabled(uint8_t t) {
  if (t == TILE_LOW) return !bidi();
  if (t == TILE_DIR) return bidi();
  if (t == TILE_CYC) return logTab();
  return true;
}

static uint16_t *tileField(uint8_t t) {
  switch (t) {
    case TILE_LOW:  return &gSet.profLowUs;
    case TILE_HIGH: return &gSet.profHighUs;
    case TILE_RUP:  return &gSet.profRampUpMs;
    case TILE_DHI:  return &gSet.profDwellHiMs;
    case TILE_RDN:  return &gSet.profRampDnMs;
    case TILE_DLO:  return &gSet.profDwellLoMs;
    case TILE_CYC:  return &gSet.profCycles;
    default:        return nullptr;
  }
}

/* Editor steps per tile: {small, big}. DIRECTION steps through its three
 * choices with the small buttons; its big buttons are disabled. */
static void tileSteps(uint8_t t, uint16_t &small, uint16_t &big) {
  switch (t) {
    case TILE_LOW: case TILE_HIGH: small = 10; big = 50;  break;
    case TILE_CYC:                 small = 1;  big = 10;  break;
    case TILE_DIR:                 small = 1;  big = 0;   break;
    default:                       small = 50; big = 500; break;
  }
}

static const char *dirName(uint8_t d) {
  return d == ESC_DIR_REV ? "REV" : d == ESC_DIR_ALT ? "FWD+REV" : "FWD";
}

static void tileValue(uint8_t t, char *out, size_t n) {
  if (t == TILE_DIR) { snprintf(out, n, "%s", bidi() ? dirName(gSet.testDir) : "FWD"); return; }
  if (t == TILE_LOW && bidi()) { snprintf(out, n, "1500"); return; }
  if (t == TILE_CYC && !logTab()) { snprintf(out, n, "CONT."); return; }
  snprintf(out, n, "%u", (unsigned)*tileField(t));
}

/* Label for the tile caption (units included) and for the editor box. */
static const char *tileCaption(uint8_t t) {
  static const char *const CAP[TILE_N] = { "LOW US", "HIGH US", "RAMP UP MS", "DWELL HI MS",
                                           "RAMP DOWN MS", "DWELL LO MS", "DIRECTION", "CYCLES" };
  if (t == TILE_LOW && bidi()) return "LOW = NEUTRAL";
  if (t == TILE_HIGH && bidi()) return "HIGH US (FWD)";
  return CAP[t];
}

/* Apply a signed step to the selected tile, clamped to its valid range. */
static void stepTile(uint8_t t, int32_t d) {
  if (t == TILE_DIR) {
    gSet.testDir = (uint8_t)((gSet.testDir + ESC_DIR_COUNT + (d > 0 ? 1 : -1)) % ESC_DIR_COUNT);
    return;
  }
  uint16_t *f = tileField(t);
  if (!f) return;
  int32_t v = (int32_t)*f + d, lo, hi;
  switch (t) {
    case TILE_LOW:  lo = ESC_ABS_MIN_US; hi = gSet.profHighUs - 10; break;
    case TILE_HIGH: lo = bidi() ? ESC_BIDI_IDLE_US + 10 : gSet.profLowUs + 10; hi = ESC_ABS_MAX_US; break;
    case TILE_RUP: case TILE_RDN: lo = 0; hi = PROF_RAMP_MAX_MS; break;
    case TILE_DHI: case TILE_DLO: lo = PROF_DWELL_MIN_MS; hi = PROF_DWELL_MAX_MS; break;
    default:        lo = 1; hi = PROF_CYCLES_MAX; break;
  }
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  *f = (uint16_t)v;
}

static uint32_t cycleMs() {
  return (uint32_t)gSet.profRampUpMs + gSet.profDwellHiMs + gSet.profRampDnMs + gSet.profDwellLoMs;
}

/* ---- chrome ----------------------------------------------------------- */
static void drawTileN(uint8_t t) {
  char v[16];
  tileValue(t, v, sizeof v);
  drawTile(TMC_T[t].vis, tileCaption(t), v, t == sSel, tileEnabled(t));
}

static void editorLabels(uint8_t which, char *out, size_t n) {
  uint16_t small, big;
  tileSteps(sSel, small, big);
  if (sSel == TILE_DIR) { snprintf(out, n, "%s", which == 1 ? "<" : which == 2 ? ">" : ""); return; }
  uint16_t s = (which == 0 || which == 3) ? big : small;
  snprintf(out, n, "%c%u", (which < 2) ? '-' : '+', (unsigned)s);
}

static bool editorEnabled(uint8_t which) {
  if (!tileEnabled(sSel) || running()) return false;
  uint16_t small, big;
  tileSteps(sSel, small, big);
  return (which == 0 || which == 3) ? big != 0 : small != 0;
}

static void drawEditorBtn(uint8_t which, bool pressed) {
  char lab[12];
  editorLabels(which, lab, sizeof lab);
  bool en = editorEnabled(which);
  drawDeltaBtn(TMC_T[TMC_BIG_M - TH_N + which].vis, pressed && en, lab,
               which == 0 || which == 3, en);
}

static void drawValueBox() {
  char v[16], line[24];
  tileValue(sSel, v, sizeof v);
  if (sSel == TILE_LOW || sSel == TILE_HIGH)        snprintf(line, sizeof line, "%s US", v);
  else if (sSel == TILE_DIR || sSel == TILE_CYC)    snprintf(line, sizeof line, "%s", v);
  else                                              snprintf(line, sizeof line, "%s MS", v);
  const JCRRect &r = TMC_VALUE_BOX;
  bool en = tileEnabled(sSel);
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, en ? COL_VOLT : COL_DISABLED_BORDER);
  tRussoCentered(RUSSO16, r.cx(), r.y + (r.h - RUSSO16.height) / 2, line,
                 en ? COL_TEXT : COL_DISABLED_TEXT, COL_BOX_FILL);
}

static void drawEditor() {
  for (uint8_t i = 0; i < 4; i++) drawEditorBtn(i, false);
  drawValueBox();
}

static const char *startLabel() {
  if (logTab()) {
    if (!escTesting()) return "START LOG TEST";
    return escPhase() == PH_POST ? "FINISHING..." : "ABORT TEST";
  }
  return escCycling() ? "STOP CYCLE" : "START CYCLE";
}
static bool startIsStop() { return logTab() ? escTesting() : escCycling(); }

static void drawStartBtn(bool pressed) {
  drawLabelBtn(TMC_T[TMC_START - TH_N].vis, pressed, startLabel(),
               startIsStop() ? COL_DANGER : COL_VOLT, startIsStop());
}
static void drawResetBtn(bool pressed) {
  drawLabelBtn(TMC_T[TMC_RESET - TH_N].vis, pressed,
               sResetArmMs ? "CONFIRM?" : "RESET DEFAULTS", sResetArmMs ? COL_AMP : COL_BOX_BORDER,
               false, !running());
}

void paintTestCycleOnce() {
  tft.fillScreen(COL_BG);
  paintTestHeader(gScreen);
  if (!tileEnabled(sSel)) sSel = TILE_HIGH;
  for (uint8_t t = 0; t < TILE_N; t++) drawTileN(t);
  drawEditor();
  drawResetBtn(false);
  drawStartBtn(false);
  updateTestCycleTick(true);
}

/* ---- tick ------------------------------------------------------------- */
void updateTestCycleTick(bool forceClear) {
  static char c1[40], c2[80];
  static bool lastRun = false, lastPost = false;
  static uint32_t lastResetArm = 0;
  if (forceClear) { c1[0] = c2[0] = 0; lastRun = running(); lastPost = escPhase() == PH_POST; lastResetArm = sResetArmMs; }

  updateTestHeaderTick(forceClear);

  /* RESET's confirm window expires on its own. */
  if (sResetArmMs && millis() - sResetArmMs > 2000) sResetArmMs = 0;
  if (sResetArmMs != lastResetArm) { lastResetArm = sResetArmMs; drawResetBtn(false); }

  /* Run state changed: button faces and editor enable/disable. */
  bool post = escPhase() == PH_POST;
  if (running() != lastRun || post != lastPost) {
    lastRun = running(); lastPost = post;
    drawStartBtn(false); drawResetBtn(false); drawEditor();
  }

  char buf[80];
  uint16_t col = COL_TEXT_HI;
  EscPhase ph = escPhase();
  uint32_t remain = escPhaseRemainMs();
  if (logTab() && escTesting()) {
    col = COL_AMP;
    if (ph == PH_PRE || ph == PH_POST)
      snprintf(buf, sizeof buf, "TEST %s  %lu.%lus%s", escPhaseName(ph), (unsigned long)(remain / 1000),
               (unsigned long)(remain % 1000 / 100), escTestAborted() ? "  ABORTED" : "");
    else
      snprintf(buf, sizeof buf, "TEST %u/%u  %s  %lu.%lus", (unsigned)escCycleNum(), (unsigned)escCycleTarget(),
               escPhaseName(ph), (unsigned long)(remain / 1000), (unsigned long)(remain % 1000 / 100));
  } else if (!logTab() && escCycling()) {
    col = COL_AMP;
    if (ph == PH_ARMING)
      snprintf(buf, sizeof buf, "ARMING  %lus", (unsigned long)((remain + 999) / 1000));
    else
      snprintf(buf, sizeof buf, "CYCLE %u  %s  %lu.%lus", (unsigned)escCycleNum(), escPhaseName(ph),
               (unsigned long)(remain / 1000), (unsigned long)(remain % 1000 / 100));
  } else if (escTesting()) {
    snprintf(buf, sizeof buf, "LOG TEST RUNNING - SEE LOG TEST TAB");
  } else if (escCycling()) {
    snprintf(buf, sizeof buf, "CYCLE RUNNING - SEE CYCLE TAB");
  } else if (logTab()) {
    uint32_t tot = ESC_TEST_PRE_MS + ESC_TEST_POST_MS + (uint32_t)gSet.profCycles * cycleMs();
    uint32_t s = (tot + 500) / 1000;
    snprintf(buf, sizeof buf, "READY  %u CYCLES  TOTAL %lu:%02lu", (unsigned)gSet.profCycles,
             (unsigned long)(s / 60), (unsigned long)(s % 60));
  } else {
    snprintf(buf, sizeof buf, "READY  1 CYCLE = %lu.%lus  %s",
             (unsigned long)(cycleMs() / 1000), (unsigned long)(cycleMs() % 1000 / 100),
             bidi() ? "BIDIRECTIONAL" : "ONE DIRECTION");
  }
  testStatusLine(buf, col, forceClear);

  /* Line 1: what is on the pin right now. */
  if (escArmed())
    snprintf(buf, sizeof buf, "OUT %u US%s", (unsigned)gEscOutUs, escCycleReverse() ? "  REV" : "");
  else if (escTesting())
    snprintf(buf, sizeof buf, "OUTPUT OFF - LOGGING POST-ROLL");
  else
    snprintf(buf, sizeof buf, "OUTPUT OFF");
  field5(8, TMC_STAT_Y1, 38, 2, escArmed() ? COL_AMP : COL_TEXT_HI, COL_BG, buf, c1);

  /* Line 2: the log, or what logging will do. */
  if (logTab()) {
    if (logTestActive())
      snprintf(buf, sizeof buf, "LOGGING %s  ROWS %lu", logFileName(), (unsigned long)logRows());
    else if (logState() == LOGST_NO_CARD || logState() == LOGST_ERROR)
      snprintf(buf, sizeof buf, "%s - INSERT CARD, REMOUNT ON LOG SCREEN", logCardText());
    else
      snprintf(buf, sizeof buf, "3 S PRE-ROLL + 5 S POST-ROLL, DISARMS WHEN DONE");
  } else {
    snprintf(buf, sizeof buf, "%s", gSet.logMode == LOGMODE_CYCLE
                                      ? "LOG MODE CYCLE: THIS RUN WILL BE LOGGED"
                                      : "NOT LOGGED - USE LOG TEST FOR A RECORDED RUN");
  }
  field5(8, TMC_STAT_Y2, 76, 1, COL_TEXT_HI, COL_BG, buf, c2);
}

/* ---- interaction ------------------------------------------------------ */
int8_t testCycleHit(int16_t x, int16_t y) {
  int8_t h = testHeaderHit(x, y);
  if (h >= 0) return h;
  for (int8_t id = TMC_TILE0; id < TMC_N; id++)
    if (TMC_T[id - TH_N].hit.contains(x, y)) return id;
  return -1;
}

bool testCycleRepeatable(int8_t id) {
  if (id < TMC_BIG_M || id > TMC_BIG_P || running() || sSel == TILE_DIR) return false;
  return editorEnabled((uint8_t)(id - TMC_BIG_M));
}

void testCycleSetPressed(int8_t id, bool pressed) {
  if (id < TH_N) { testHeaderSetPressed(gScreen, id, pressed); return; }
  if (id >= TMC_BIG_M && id <= TMC_BIG_P) drawEditorBtn((uint8_t)(id - TMC_BIG_M), pressed);
  else if (id == TMC_START) drawStartBtn(pressed);
  else if (id == TMC_RESET) drawResetBtn(pressed && !running());
}

static void startLogTest() {
  if (escCycling())   { showToast("Stop the cycle first"); return; }
  /* The test arms itself. Starting from disarmed also means a card mount
   * (up to ~2 s of blocking with no card) can never happen while a motor is
   * under manual throttle with STOP unresponsive. */
  if (escArmed())     { showToast("Disarm first - the test arms itself"); return; }
  if (logRecording()) { showToast("Stop the current log first"); return; }
  if (!logStartTest()) { showToast("Could not open a log - check SD"); return; }
  if (!escTestStart(gSet.profCycles)) {
    logStop("not started");
    showToast("ESC output unavailable");
  }
}

void testCycleDispatch(int8_t id) {
  if (testHeaderDispatch(gScreen, id)) return;

  if (id >= TMC_TILE0 && id < TMC_TILE0 + (int)TILE_N) {
    uint8_t t = (uint8_t)(id - TMC_TILE0);
    if (!tileEnabled(t)) {
      showToast(t == TILE_LOW ? "Bidirectional: low end = neutral 1500" :
                t == TILE_DIR ? "Direction applies to bidirectional ESCs" :
                                "CYCLE runs until STOP");
      return;
    }
    uint8_t old = sSel;
    sSel = t;
    drawTileN(old);
    drawTileN(t);
    drawEditor();
    return;
  }

  if (id >= TMC_BIG_M && id <= TMC_BIG_P) {
    uint8_t which = (uint8_t)(id - TMC_BIG_M);
    if (running()) { showToast("Stop the run to edit"); return; }
    if (!editorEnabled(which)) return;
    uint16_t small, big;
    tileSteps(sSel, small, big);
    int32_t mag = (which == 0 || which == 3) ? big : small;
    stepTile(sSel, which < 2 ? -mag : mag);
    drawTileN(sSel);
    drawValueBox();
    return;
  }

  if (id == TMC_RESET) {
    if (running()) { showToast("Stop the run first"); return; }
    if (!sResetArmMs) {
      sResetArmMs = millis();
      if (!sResetArmMs) sResetArmMs = 1;
      drawResetBtn(false);
      return;
    }
    sResetArmMs = 0;
    settingsProfileDefaults(gSet);
    paintScreen(gScreen);
    showToast("Profile reset to defaults");
    return;
  }

  if (id == TMC_START) {
    if (logTab()) {
      if (escTesting()) {
        if (escPhase() != PH_POST) escTestAbort();
      } else {
        startLogTest();
      }
    } else {
      if (escCycling())      escCycleStop();
      else if (escTesting()) showToast("Log Test running");
      else if (!escCycleStart()) showToast("ESC output unavailable");
    }
    drawStartBtn(true);
    updateTestCycleTick();
  }
}
