#include "Screens.h"
#include "Widgets.h"
#include "Config.h"

ScreenId gScreen = SCR_HOME;

void paintScreen(ScreenId s) {
  switch (s) {
    case SCR_HOME:  paintHomeOnce();  break;
    case SCR_LIVE:  paintLiveOnce();  break;
    case SCR_GRAPH: paintGraphOnce(); break;
    case SCR_CAL:   paintCalOnce();   break;
    case SCR_DEV:   paintDevOnce();   break;
  }
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
    case SCR_CAL:   return hitTargets(CAL_T,   1,            x, y);
    case SCR_DEV:   return hitTargets(DEV_T,   1,            x, y);
  }
  return -1;
}

void dispatch(ScreenId s, int8_t id) {
  switch (s) {
    case SCR_HOME:  homeDispatch(id);  break;
    case SCR_LIVE:  liveDispatch(id);  break;
    case SCR_GRAPH: graphDispatch(id); break;
    case SCR_CAL:   calDispatch(id);   break;
    case SCR_DEV:   devDispatch(id);   break;
  }
}

void setPressedVisual(ScreenId s, int8_t id, bool pressed) {
  switch (s) {
    case SCR_HOME:  homeSetPressed(id, pressed);  break;
    case SCR_LIVE:  liveSetPressed(id, pressed);  break;
    case SCR_GRAPH: graphSetPressed(id, pressed); break;
    case SCR_CAL:   calSetPressed(id, pressed);   break;
    case SCR_DEV:   devSetPressed(id, pressed);   break;
  }
}

/* Home and Calibrate have nothing that changes per tick. */
void tickScreen(ScreenId s) {
  static uint32_t lastLive = 0, lastGraph = 0, lastDev = 0;
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
        Serial.printf("[LAYOUT BUG] %s: hit rects %u and %u overlap\n", screen, a, b);
    }
}
void checkTargetOverlaps() {
  warnOverlaps(HOME_T,  HOME_N,      "HOME");
  warnOverlaps(LIVE_T,  LIVE_BTN_N,  "LIVE");
  warnOverlaps(GRAPH_T, GRAPH_BTN_N, "GRAPH");
}
