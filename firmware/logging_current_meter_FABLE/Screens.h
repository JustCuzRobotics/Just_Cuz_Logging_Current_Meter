/* ==========================================================================
 * Screens.h — navigation and the per-screen interface.
 *
 * Navigation is flat: Home plus its subscreens, every Back returns to Home,
 * so no stack is needed. Settings is the one exception — Dev Mode is reached
 * from it, and Dev's Back still goes Home rather than retracing a step.
 *
 * Each screen supplies four functions; the dispatch table in Screens.cpp is
 * the only place that needs to know they all exist.
 *
 * Every tick function takes `forceClear` — pass true whenever the pixels
 * under its cached fields were wiped (screen entry, toast expiry). See the
 * note at the top of Widgets.h for what happens if you forget.
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>
#include "Layout.h"

enum ScreenId { SCR_HOME, SCR_LIVE, SCR_GRAPH, SCR_SETTINGS, SCR_DEV, SCR_LOG,
                SCR_TEST_MANUAL, SCR_TEST_CYCLE, SCR_TEST_LOG };
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

/* ---- Test Mode: three tabs ----
 * MANUAL, CYCLE and LOG TEST share the header in ScreenTestCommon.cpp. CYCLE
 * and LOG TEST are one implementation (ScreenTestCycle.cpp) with a flag. */
ScreenId testTabScreen(uint8_t tab);      /* 0 manual, 1 cycle, 2 log test */
void paintTestHeader(ScreenId s);
void testHeaderSetPressed(ScreenId s, int8_t id, bool pressed);
bool testHeaderDispatch(ScreenId s, int8_t id);   /* true if it was a header id */
void updateTestHeaderTick(bool forceClear);
void testStatusLine(const char *text, uint16_t color, bool forceClear);
int8_t testHeaderHit(int16_t x, int16_t y);

void   paintTestManualOnce();
void   updateTestManualTick(bool forceClear = false);
int8_t testManualHit(int16_t x, int16_t y);
void   testManualSetPressed(int8_t id, bool pressed);
void   testManualDispatch(int8_t id);
void   testManualDrag(int8_t id, int16_t x, int16_t y);
void   testManualRelease(int8_t id);
bool   testManualIsDrag(int8_t id);

void   paintTestCycleOnce();              /* CYCLE or LOG TEST, from gScreen */
void   updateTestCycleTick(bool forceClear = false);
int8_t testCycleHit(int16_t x, int16_t y);
void   testCycleSetPressed(int8_t id, bool pressed);
void   testCycleDispatch(int8_t id);

/* ---- press-and-hold, drag ----
 * pumpTouchEvents() calls these for the target that is currently held:
 * repeatable ids re-dispatch every 200 ms after 1 s; drag ids get the live
 * finger position every loop and a release call when the finger lifts. */
bool screenRepeatable(ScreenId s, int8_t id);
bool testManualRepeatable(int8_t id);
bool testCycleRepeatable(int8_t id);
extern int16_t gDownX, gDownY;            /* last press position (.ino)    */
bool screenIsDrag(ScreenId s, int8_t id);
void screenDrag(ScreenId s, int8_t id, int16_t x, int16_t y);
void screenRelease(ScreenId s, int8_t id);

void paintSettingsOnce();
void updateSettingsTick(bool forceClear = false);
void settingsSetPressed(int8_t id, bool pressed);
void settingsDispatch(int8_t id);
void settingsApplyFilter();          /* push gSet.filterIndex to the sampler */
void dumpCalibrationToSerial();      /* what the Calibrate screen used to be */

void paintLogOnce();
void updateLogTick(bool forceClear = false);
void logSetPressed(int8_t id, bool pressed);
void logDispatch(int8_t id);

void paintDevOnce();
void updateDevTick(bool forceClear = false);
void devSetPressed(int8_t id, bool pressed);
void devDispatch(int8_t id);
extern uint32_t gDevTaps, gLoopUsAvg;
extern uint32_t gLatUsAvg, gLatUsMax, gLoopUsMax;   /* .ino: per-second window */
