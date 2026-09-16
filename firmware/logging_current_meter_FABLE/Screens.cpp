#include "Screens.h"
#include "Widgets.h"
#include "Config.h"

ScreenId gScreen = SCR_HOME;

void paintScreen(ScreenId s) {
  switch (s) {
    case SCR_HOME:  paintHomeOnce();  break;
    case SCR_LIVE:  paintLiveOnce();  break;
    case SCR_GRAPH: paintGraphOnce(); break;
    case SCR_TEST:     paintTestOnce();     break;
    case SCR_SETTINGS: paintSettingsOnce(); break;
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
    case SCR_TEST:     return hitTargets(TEST_T, TEST_BTN_N, x, y);
    case SCR_SETTINGS: return hitTargets(SET_T,  SET_BTN_N,  x, y);
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
    case SCR_TEST:     testDispatch(id);     break;
    case SCR_SETTINGS: settingsDispatch(id); break;
    case SCR_DEV:   devDispatch(id);   break;
    case SCR_LOG:   logDispatch(id);   break;
  }
}

void setPressedVisual(ScreenId s, int8_t id, bool pressed) {
  switch (s) {
    case SCR_HOME:  homeSetPressed(id, pressed);  break;
    case SCR_LIVE:  liveSetPressed(id, pressed);  break;
    case SCR_GRAPH: graphSetPressed(id, pressed); break;
    case SCR_TEST:     testSetPressed(id, pressed);     break;
    case SCR_SETTINGS: settingsSetPressed(id, pressed); break;
    case SCR_DEV:   devSetPressed(id, pressed);   break;
    case SCR_LOG:   logSetPressed(id, pressed);   break;
  }
}

/* Home has nothing that changes per tick. Test Mode does, but only because
 * of the auto-cycle countdown, so it runs at the slow Dev cadence. */
void tickScreen(ScreenId s) {
  static uint32_t lastLive = 0, lastGraph = 0, lastDev = 0, lastTest = 0, lastLog = 0;
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
    case SCR_TEST:
      if (now - lastTest >= DEV_FRAME_MS) { lastTest = now; updateTestTick(); }
      break;
    case SCR_LOG:
      if (now - lastLog >= DEV_FRAME_MS) { lastLog = now; updateLogTick(); }
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
  warnOverlaps(TEST_T,  TEST_BTN_N,  "TEST");
  warnOverlaps(SET_T,   SET_BTN_N,   "SETTINGS");
  warnOverlaps(DEV_T,   DEV_BTN_N,   "DEV");
  warnOverlaps(LOG_T,   LOG_BTN_N,   "LOG");
}
