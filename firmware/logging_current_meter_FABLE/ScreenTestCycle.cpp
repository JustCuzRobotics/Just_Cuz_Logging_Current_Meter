/* ==========================================================================
 * ScreenTestCycle.cpp — Test Mode, CYCLE and LOG TEST tabs.
 *
 * Both tabs edit ONE saved profile (gSet.prof*):
 *
 *   [LOW/STOP][SPIN US][RAMP UP][DWELL HI]    tap a tile to select it
 *   [RAMP DN ][DWELL LO][CYC MODE][CYCLES]
 *   [-BIG][-SMALL]   value   [+SMALL][+BIG]   hold 1 s to repeat, 5 per s
 *   status
 *   [RESET DEFAULTS]        [START ... / STOP ...]
 *
 * CYCLE runs RAMP UP -> DWELL HI -> RAMP DOWN -> DWELL LO until stopped.
 * LOG TEST opens LOG_<n>_TEST_<V>V.CSV, then the pre-roll at idle (ESC
 * PRE-ROLL in Settings, with the 2 s arming hold inside it), N cycles, a 5 s
 * post-roll, and closes the log. The output stays live at neutral afterwards
 * so the next test starts without the ESC's start-up, and a fuse cuts it if
 * nothing else happens. Aborting jumps to the post-roll so the spin-down is
 * still logged.
 *
 * The profile is locked while a run is active: EscOut snapshots it at start,
 * and the log header records it, so an edit mid-run would change neither —
 * refusing it avoids the screen claiming a value the motor is not seeing.
 *
 * CYCLE MODE decides what the bottom of a cycle is:
 *   STOP -> SPIN  the low end is the ESC type's idle (1000 or 1500), locked,
 *                 and the LOW tile is greyed. SPIN US is a free 1000-2000
 *                 pulse, so a bidirectional ESC runs backwards simply by
 *                 setting it below 1500 — the number on the screen is the
 *                 number on the pin, with no mirroring and no direction
 *                 setting. (The ESC has to be in its own 3D mode for a pulse
 *                 under neutral to mean reverse.)
 *   SPIN -> SPIN  both ends are free 1000-2000, for cycling between two
 *                 running points rather than through a stop.
 *
 * Tiles that do not apply are greyed and unselectable: LOW in STOP -> SPIN,
 * CYCLES on the CYCLE tab (continuous).
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

static bool stopSpin() { return gSet.cycleMode == CYC_STOP_SPIN; }

static bool tileEnabled(uint8_t t) {
  if (t == TILE_LOW) return !stopSpin();   /* locked to the type's idle    */
  if (t == TILE_CYC) return logTab();
  return true;
}

/* Read/write by tile. The dwells are 32-bit (up to 3 minutes) and everything
 * else 16, so these go through a common uint32_t rather than a pointer. */
static uint32_t tileGet(uint8_t t) {
  switch (t) {
    case TILE_LOW:  return gSet.profLowUs;
    case TILE_HIGH: return gSet.profHighUs;
    case TILE_RUP:  return gSet.profRampUpMs;
    case TILE_DHI:  return gSet.profDwellHiMs;
    case TILE_RDN:  return gSet.profRampDnMs;
    case TILE_DLO:  return gSet.profDwellLoMs;
    case TILE_CYC:  return gSet.profCycles;
    default:        return 0;
  }
}

static void tilePut(uint8_t t, uint32_t v) {
  switch (t) {
    case TILE_LOW:  gSet.profLowUs     = (uint16_t)v; break;
    case TILE_HIGH: gSet.profHighUs    = (uint16_t)v; break;
    case TILE_RUP:  gSet.profRampUpMs  = (uint16_t)v; break;
    case TILE_DHI:  gSet.profDwellHiMs = v;           break;
    case TILE_RDN:  gSet.profRampDnMs  = (uint16_t)v; break;
    case TILE_DLO:  gSet.profDwellLoMs = v;           break;
    case TILE_CYC:  gSet.profCycles    = (uint16_t)v; break;
    default: break;
  }
}

/* Editor steps per tile: {small, big}. DIRECTION steps through its three
 * settings with the small buttons; its big buttons are disabled.
 *
 * Millisecond tiles change gear: below 5 s the fine pair (50 / 500 ms) is what
 * you want for a ramp, and above it the coarse pair (1 s / 10 s), because a
 * 3-minute dwell set 500 ms at a time is 360 taps. The gear follows the value
 * itself, so it changes under you as you cross 5 s — the buttons relabel to
 * match. */
static void tileSteps(uint8_t t, uint32_t &small, uint32_t &big) {
  switch (t) {
    case TILE_LOW: case TILE_HIGH: small = 10; big = 50;  break;
    case TILE_CYC:                 small = 1;  big = 10;  break;
    case TILE_MODE:                small = 1;  big = 0;   break;
    default:
      if (tileGet(t) > PROF_MS_COARSE_ABOVE) { small = 1000; big = 10000; }
      else                                   { small = 50;   big = 500;   }
      break;
  }
}

static const char *modeName() { return stopSpin() ? "STOP-SPIN" : "SPIN-SPIN"; }

static void tileValue(uint8_t t, char *out, size_t n) {
  if (t == TILE_MODE) { snprintf(out, n, "%s", modeName()); return; }
  if (t == TILE_LOW && stopSpin()) { snprintf(out, n, "%u", (unsigned)escIdleUs()); return; }
  if (t == TILE_CYC && !logTab()) { snprintf(out, n, "CONT."); return; }
  snprintf(out, n, "%lu", (unsigned long)tileGet(t));
}

/* Label for the tile caption (units included) and for the editor box. */
static const char *tileCaption(uint8_t t) {
  static const char *const CAP[TILE_N] = { "LOW US", "SPIN US", "RAMP UP MS", "DWELL HI MS",
                                           "RAMP DOWN MS", "DWELL LO MS", "CYCLE MODE", "CYCLES" };
  if (t == TILE_LOW)  return stopSpin() ? (bidi() ? "LOW = NEUTRAL" : "LOW = STOP") : "LOW US";
  if (t == TILE_HIGH) return stopSpin() ? "SPIN US" : "HIGH US";
  return CAP[t];
}

/* Apply a signed step to the selected tile, clamped to its valid range. */
static void stepTile(uint8_t t, int32_t d) {
  if (t == TILE_MODE) {
    gSet.cycleMode = (uint8_t)((gSet.cycleMode + CYC_MODE_COUNT + (d > 0 ? 1 : -1)) % CYC_MODE_COUNT);
    return;
  }
  int32_t v = (int32_t)tileGet(t) + d, lo, hi;
  switch (t) {
    /* Both ends are plain pulses now, anywhere in the band and in either
     * order: a cycle from 1750 down to 1250 is as valid as the other way up,
     * and on a bidirectional ESC that is how you run one side to the other. */
    case TILE_LOW: case TILE_HIGH: lo = ESC_ABS_MIN_US; hi = ESC_ABS_MAX_US; break;
    case TILE_RUP: case TILE_RDN: lo = 0; hi = PROF_RAMP_MAX_MS; break;
    case TILE_DHI: case TILE_DLO: lo = PROF_DWELL_MIN_MS; hi = PROF_DWELL_MAX_MS; break;
    default:        lo = 1; hi = PROF_CYCLES_MAX; break;
  }
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  tilePut(t, (uint32_t)v);
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
  uint32_t small, big;
  tileSteps(sSel, small, big);
  if (sSel == TILE_MODE) { snprintf(out, n, "%s", which == 1 ? "<" : which == 2 ? ">" : ""); return; }
  uint32_t s = (which == 0 || which == 3) ? big : small;
  char sign = (which < 2) ? '-' : '+';
  /* A whole number of seconds reads as "+10 S" rather than "+10000" — five
   * digits would drop the label to the 5x7 fallback size anyway. */
  if (s >= 1000 && s % 1000 == 0) snprintf(out, n, "%c%lu S", sign, (unsigned long)(s / 1000));
  else                            snprintf(out, n, "%c%lu", sign, (unsigned long)s);
}

static bool editorEnabled(uint8_t which) {
  if (!tileEnabled(sSel) || running()) return false;
  uint32_t small, big;
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
  else if (sSel == TILE_MODE || sSel == TILE_CYC)   snprintf(line, sizeof line, "%s", v);
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
    if (ph == PH_PRE)
      snprintf(buf, sizeof buf, "PRE-ROLL AT IDLE  %lu.%lus", (unsigned long)(remain / 1000),
               (unsigned long)(remain % 1000 / 100));
    else
      snprintf(buf, sizeof buf, "CYCLE %u  %s  %lu.%lus", (unsigned)escCycleNum(), escPhaseName(ph),
               (unsigned long)(remain / 1000), (unsigned long)(remain % 1000 / 100));
  } else if (escTesting()) {
    snprintf(buf, sizeof buf, "LOG TEST RUNNING - SEE LOG TEST TAB");
  } else if (escCycling()) {
    snprintf(buf, sizeof buf, "CYCLE RUNNING - SEE CYCLE TAB");
  } else if (logTab()) {
    uint32_t tot = gSet.preRollMs + ESC_TEST_POST_MS + (uint32_t)gSet.profCycles * cycleMs();
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
    snprintf(buf, sizeof buf, "OUT %u US%s", (unsigned)gEscOutUs, escOutIsReverse() ? "  REV" : "");
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
      snprintf(buf, sizeof buf, "%u.%u S PRE-ROLL + 5 S POST-ROLL, ENDS AT IDLE",
               (unsigned)(gSet.preRollMs / 1000), (unsigned)((gSet.preRollMs % 1000) / 100));
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
  if (id < TH_N) return testHeaderRepeatable(id);
  if (id < TMC_BIG_M || id > TMC_BIG_P || running() || sSel == TILE_MODE) return false;
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
  if (logRecording()) { showToast("Stop the current log first"); return; }
  /* An already-live output is commanded to neutral before anything else, not
   * refused: pressing START means start. Neutral first also means the card
   * mount below (up to ~2 s of blocking with no card, during which STOP does
   * not answer) can never happen with a motor under throttle. */
  escNeutral();
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
      showToast(t == TILE_LOW ? "STOP-SPIN: low end is the ESC's stop pulse" :
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
    uint32_t small, big;
    tileSteps(sSel, small, big);
    int32_t mag = (int32_t)((which == 0 || which == 3) ? big : small);
    stepTile(sSel, which < 2 ? -mag : mag);
    drawTileN(sSel);
    /* The mode decides whether LOW is the locked stop pulse and whether HIGH
     * is captioned SPIN US, so both tiles change face with it. */
    if (sSel == TILE_MODE) { drawTileN(TILE_LOW); drawTileN(TILE_HIGH); }
    drawValueBox();
    /* Crossing 5 s changes which gear the buttons are in, so relabel them. */
    for (uint8_t w = 0; w < 4; w++) drawEditorBtn(w, w == which);
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
