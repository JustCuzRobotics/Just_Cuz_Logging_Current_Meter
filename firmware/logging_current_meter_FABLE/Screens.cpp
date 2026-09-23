#include "Screens.h"
#include "Widgets.h"
#include "Config.h"

ScreenId gScreen = SCR_HOME;

void paintScreen(ScreenId s) {
  switch (s) {
    case SCR_HOME:  paintHomeOnce();  break;
    case SCR_LIVE:  paintLiveOnce();  break;
    case SCR_GRAPH: paintGraphOnce(); break;
    case SCR_TEST_MANUAL: paintTestManualOnce(); break;
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:    paintTestCycleOnce();  break;
    case SCR_SETTINGS: paintSettingsOnce(); break;
    case SCR_CLOCK: paintClockOnce(); break;
    case SCR_DEV:   paintDevOnce();   break;
    case SCR_LOG:   paintLogOnce();   break;
  }
  logBarPaint();
  /* Every full repaint clears the top edge, so the armed strip is restored
   * here rather than in fifteen places. This also covers a toast expiring,
   * which repaints through this same function. */
  escBarPaint();
}
void goTo(ScreenId s) { gScreen = s; paintScreen(s); }

static int8_t hitTargets(const Target *t, uint8_t n, int16_t x, int16_t y) {
  for (uint8_t i = 0; i < n; i++)
    if (t[i].hit.contains(x, y)) return (int8_t)i;
  return -1;
}

int8_t hitTestScreen(ScreenId s, int16_t x, int16_t y) {
  switch (s) {
    case SCR_HOME:  return hitTargets(HOME_T,  HOME_N,       x, y);
    case SCR_LIVE:  return hitTargets(LIVE_T,  LIVE_BTN_N,   x, y);
    case SCR_GRAPH: return hitTargets(GRAPH_T, GRAPH_BTN_N,  x, y);
    case SCR_TEST_MANUAL: return testManualHit(x, y);
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:    return testCycleHit(x, y);
    case SCR_SETTINGS: return hitTargets(SET_T,  SET_BTN_N,  x, y);
    case SCR_CLOCK: return clockHit(x, y);
    case SCR_DEV:   return hitTargets(DEV_T,   DEV_BTN_N,    x, y);
    case SCR_LOG:   return hitTargets(LOG_T,   LOG_BTN_N,    x, y);
  }
  return -1;
}

void dispatch(ScreenId s, int8_t id) {
  switch (s) {
    case SCR_HOME:  homeDispatch(id);  break;
    case SCR_LIVE:  liveDispatch(id);  break;
    case SCR_GRAPH: graphDispatch(id); break;
    case SCR_TEST_MANUAL: testManualDispatch(id); break;
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:    testCycleDispatch(id);  break;
    case SCR_SETTINGS: settingsDispatch(id); break;
    case SCR_CLOCK: clockDispatch(id); break;
    case SCR_DEV:   devDispatch(id);   break;
    case SCR_LOG:   logDispatch(id);   break;
  }
}

void setPressedVisual(ScreenId s, int8_t id, bool pressed) {
  switch (s) {
    case SCR_HOME:  homeSetPressed(id, pressed);  break;
    case SCR_LIVE:  liveSetPressed(id, pressed);  break;
    case SCR_GRAPH: graphSetPressed(id, pressed); break;
    case SCR_TEST_MANUAL: testManualSetPressed(id, pressed); break;
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:    testCycleSetPressed(id, pressed);  break;
    case SCR_SETTINGS: settingsSetPressed(id, pressed); break;
    case SCR_CLOCK: clockSetPressed(id, pressed); break;
    case SCR_DEV:   devSetPressed(id, pressed);   break;
    case SCR_LOG:   logSetPressed(id, pressed);   break;
  }
}

/* Home has nothing that changes per tick. Test Mode does, but only because
 * of the auto-cycle countdown, so it runs at the slow Dev cadence. */
void tickScreen(ScreenId s) {
  static uint32_t lastLive = 0, lastGraph = 0, lastDev = 0, lastTest = 0, lastLog = 0, lastClock = 0;
  uint32_t now = millis();
  switch (s) {
    case SCR_LIVE:
      if (now - lastLive >= LIVE_FRAME_MS) { lastLive = now; updateLiveTick(); }
      break;
    case SCR_GRAPH:
      if (now - lastGraph >= GRAPH_FRAME_MS) {
        lastGraph = now;
        updateGraphTick(gGraphForceRedraw);
        gGraphForceRedraw = false;
      }
      break;
    case SCR_DEV:
      if (now - lastDev >= DEV_FRAME_MS) { lastDev = now; updateDevTick(); }
      break;
    case SCR_TEST_MANUAL:
      if (now - lastTest >= DEV_FRAME_MS) { lastTest = now; updateTestManualTick(); }
      break;
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:
      if (now - lastTest >= DEV_FRAME_MS) { lastTest = now; updateTestCycleTick(); }
      break;
    case SCR_LOG:
      if (now - lastLog >= DEV_FRAME_MS) { lastLog = now; updateLogTick(); }
      break;
    /* The clock screen only has a seconds hand to move. */
    case SCR_CLOCK:
      if (now - lastClock >= 250) { lastClock = now; updateClockTick(); }
      break;
    default: break;
  }
}

static void warnOverlaps(const Target *t, uint8_t n, const char *screen) {
  for (uint8_t a = 0; a < n; a++)
    for (uint8_t b = (uint8_t)(a + 1); b < n; b++) {
      const JCRRect &p = t[a].hit, &q = t[b].hit;
      bool separate = (p.x + p.w <= q.x) || (q.x + q.w <= p.x) ||
                      (p.y + p.h <= q.y) || (q.y + q.h <= p.y);
      if (!separate)
        Serial.printf("# [LAYOUT BUG] %s: hit rects %u and %u overlap\n", screen, a, b);
    }
}
void checkTargetOverlaps() {
  warnOverlaps(HOME_T,  HOME_N,      "HOME");
  warnOverlaps(LIVE_T,  LIVE_BTN_N,  "LIVE");
  warnOverlaps(GRAPH_T, GRAPH_BTN_N, "GRAPH");
  warnOverlaps(TH_T,    TH_N,        "TEST HEADER");
  /* The manual tab's stepper row and slider share a band and are never both
   * active, so check each mode's set on its own (plus the always-on rows). */
  {
    Target stepMode[TMM_N], sliderMode[TMM_N];
    uint8_t ns = 0, nl = 0;
    for (uint8_t i = 0; i < TH_N; i++) { stepMode[ns++] = TH_T[i]; sliderMode[nl++] = TH_T[i]; }
    for (uint8_t i = TMM_ESC; i < TMM_N; i++) {
      const Target &t = TMM_T[i - TH_N];
      if (i != TMM_SLIDER && i != TMM_RELEASE) stepMode[ns++] = t;
      if (i < TMM_M50 || i > TMM_P50) sliderMode[nl++] = t;
    }
    warnOverlaps(stepMode, ns, "TEST MANUAL (buttons)");
    warnOverlaps(sliderMode, nl, "TEST MANUAL (slider)");
  }
  {
    Target all[TMC_N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < TH_N; i++) all[n++] = TH_T[i];
    for (uint8_t i = 0; i < TMC_N - TH_N; i++) all[n++] = TMC_T[i];
    warnOverlaps(all, n, "TEST CYCLE");
  }
  warnOverlaps(SET_T,   SET_BTN_N,   "SETTINGS");
  warnOverlaps(CLK_T,   CLK_BTN_N,   "SET CLOCK");
  warnOverlaps(DEV_T,   DEV_BTN_N,   "DEV");
  warnOverlaps(LOG_T,   LOG_BTN_N,   "LOG");
}

/* ---- press-and-hold, drag ------------------------------------------------ */
bool screenRepeatable(ScreenId s, int8_t id) {
  switch (s) {
    /* Repeat only when the press is doing something: a refused press would
     * otherwise re-toast five times a second. */
    case SCR_TEST_MANUAL: return testManualRepeatable(id);
    case SCR_TEST_CYCLE:
    case SCR_TEST_LOG:    return testCycleRepeatable(id);
    case SCR_LOG:         return id >= LOG_RATE_M && id <= LOG_DUR_P;
    case SCR_SETTINGS:    return id == SET_FILTER_M || id == SET_FILTER_P ||
                                 id == SET_PRE_M || id == SET_PRE_P;
    case SCR_CLOCK:       return clockRepeatable(id);
    default:              return false;
  }
}
bool screenIsDrag(ScreenId s, int8_t id) {
  return s == SCR_TEST_MANUAL && testManualIsDrag(id);
}
void screenDrag(ScreenId s, int8_t id, int16_t x, int16_t y) {
  if (s == SCR_TEST_MANUAL) testManualDrag(id, x, y);
}
void screenRelease(ScreenId s, int8_t id) {
  if (s == SCR_TEST_MANUAL) testManualRelease(id);
}
