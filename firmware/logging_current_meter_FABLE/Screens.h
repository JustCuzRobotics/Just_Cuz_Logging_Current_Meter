/* ==========================================================================
 * Screens.h — navigation and the per-screen interface.
 *
 * Navigation is flat: Home plus four subscreens, every Back returns to Home,
 * so no stack is needed. Each screen supplies four functions; the dispatch
 * table in Screens.cpp is the only place that needs to know they all exist.
 *
 * Every tick function takes `forceClear` — pass true whenever the pixels
 * under its cached fields were wiped (screen entry, toast expiry). See the
 * note at the top of Widgets.h for what happens if you forget.
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>
#include "Layout.h"

enum ScreenId { SCR_HOME, SCR_LIVE, SCR_GRAPH, SCR_CAL, SCR_DEV };
extern ScreenId gScreen;

void   paintScreen(ScreenId s);
void   goTo(ScreenId s);
int8_t hitTestScreen(ScreenId s, int16_t x, int16_t y);
void   dispatch(ScreenId s, int8_t id);
void   setPressedVisual(ScreenId s, int8_t id, bool pressed);
void   tickScreen(ScreenId s);              /* frame-gated per-screen update */

/* Boot-time layout check: two hit rects overlapping on one screen is a bug
 * you want reported, not discovered by a mis-hit six months later. */
void checkTargetOverlaps();

/* ---- per-screen entry points ---- */
void paintHomeOnce();
void homeSetPressed(int8_t id, bool pressed);
void homeDispatch(int8_t id);

void paintLiveOnce();
void updateLiveTick(bool forceClear = false);
void liveSetPressed(int8_t id, bool pressed);
void liveDispatch(int8_t id);

void paintGraphOnce();
void updateGraphTick(bool forceClear = false);
void graphSetPressed(int8_t id, bool pressed);
void graphDispatch(int8_t id);
extern bool gGraphForceRedraw;              /* set after a V/T chip toggle */

void paintCalOnce();
void calSetPressed(int8_t id, bool pressed);
void calDispatch(int8_t id);

void paintDevOnce();
void updateDevTick(bool forceClear = false);
void devSetPressed(int8_t id, bool pressed);
void devDispatch(int8_t id);
extern uint32_t gDevTaps, gLoopUsAvg;
