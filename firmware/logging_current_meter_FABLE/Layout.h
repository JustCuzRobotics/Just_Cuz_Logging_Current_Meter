/* ==========================================================================
 * Layout.h — every pixel coordinate in the UI, in one place.
 *
 * Tuned for 480x320 (LCD_ROTATION 1). The library itself is resolution- and
 * rotation-agnostic; this app is not, by choice — the screens are hand-placed
 * rather than reflowed. Retargeting to another size means editing this file
 * and nothing else, which is why every rect lives here even when only one
 * screen uses it.
 *
 * ---- Touch targets ----
 * Each interactive control carries TWO rects: `vis` is what gets drawn, `hit`
 * is what actually registers a press. The hit rects are deliberately much
 * larger — corner controls bleed to the screen edges, interior ones claim the
 * dead space around them, and where two controls face each other the gap is
 * split. That replaces the old uniform slop, which could not make a corner
 * button reach the corner.
 *
 * checkTargetOverlaps() verifies at boot that no two hit rects on the same
 * screen overlap; Dev Mode draws the real hit outlines so they can be
 * inspected on hardware.
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>

struct Target { JCRRect vis; JCRRect hit; };

/* ------------------------------------------------------------------ HOME --
 * Five tiles; hit rects grown 6 px per side, closing the 14 px gaps to 2. */
enum { HOME_LIVE, HOME_GRAPH, HOME_LOG, HOME_CAL, HOME_DEV, HOME_N };
inline const Target HOME_T[HOME_N] = {
  { { 20,  44, 213, 121}, { 14,  38, 225, 133} },
  { {247,  44, 213, 121}, {241,  38, 225, 133} },
  { { 20, 179, 137, 121}, { 14, 173, 149, 133} },   /* Log — disabled stub */
  { {171, 179, 137, 121}, {165, 173, 149, 133} },
  { {322, 179, 138, 121}, {316, 173, 150, 133} },
};
inline const char *const HOME_LABEL[HOME_N] = { "LIVE VIEW", "GRAPH", "LOG", "CALIBRATE", "DEV MODE" };
inline const char *const HOME_SUB[HOME_N]   = { "V I T W ENERGY", "SCALED PLOT, PEAKS",
                                                "START/STOP", "VIEW CONSTANTS", "TOUCH + FPS DEBUG" };
inline const bool HOME_DISABLED[HOME_N]     = { false, false, true, false, false };

/* ------------------------------------------------------------- LIVE VIEW -- */
inline const JCRRect LIVE_TIMER_BOX = { 348,   8,  86, 30 };
inline const JCRRect LIVE_STAT[4]   = { {8,42,110,76}, {126,42,110,76}, {244,42,110,76}, {362,42,110,76} };
inline const JCRRect LIVE_ENERGY[2] = { { 46, 134, 210, 44 }, { 262, 134, 210, 44 } };
inline const JCRRect LIVE_EST[2]    = { {  8, 196, 229, 44 }, { 243, 196, 229, 44 } };

enum { LIVE_BTN_BACK, LIVE_BTN_TIMER_RST, LIVE_BTN_ENERGY_RST,
       LIVE_BTN_TARE, LIVE_BTN_PEAK_RST, LIVE_BTN_N };
inline const Target LIVE_T[LIVE_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  96, 52} },   /* Back      -> corner    */
  { {440,   8,  32, 30}, {436,   0,  44, 52} },   /* timer R   -> corner    */
  { {  8, 134,  32, 44}, {  0, 126,  48, 60} },   /* energy R  -> left edge */
  { {  8, 250, 226, 40}, {  0, 242, 240, 78} },   /* TARE      -> corner    */
  { {246, 250, 226, 40}, {240, 242, 240, 78} },   /* PEAK RESET-> corner    */
};
inline const char *const LIVE_STAT_LABEL[4] = { "VOLT", "CURRENT", "TEMP", "POWER" };

/* ----------------------------------------------------------------- GRAPH -- */
#define PLOT_X0   38
#define PLOT_Y0   43
#define PLOT_W   380
#define PLOT_H   183
#define PLOT_X1  (PLOT_X0 + PLOT_W)
#define PLOT_Y1  (PLOT_Y0 + PLOT_H)
#define TEMP_LO_CENTIDEGC 1500

inline const JCRRect GRAPH_PRESENT[4] = { {46,6,102,30}, {154,6,102,30}, {262,6,102,30}, {370,6,102,30} };
inline const JCRRect GRAPH_CHIP_I     = {   8, 246, 32, 24 };   /* non-interactive */
inline const JCRRect GRAPH_TIMER_BOX  = { 182, 246, 66, 24 };
inline const JCRRect GRAPH_PEAK[4]    = { {46,280,102,32}, {154,280,102,32}, {262,280,102,32}, {370,280,102,32} };
inline const char *const GRAPH_PRESENT_LABEL[4] = { "I", "P", "V", "T" };
inline const char *const GRAPH_PEAK_LABEL[4]    = { "I:", "P:", "V:", "T:" };

enum { GRAPH_BTN_BACK, GRAPH_BTN_CHIP_V, GRAPH_BTN_CHIP_T,
       GRAPH_BTN_TIMER_RST, GRAPH_BTN_PEAK_RST, GRAPH_BTN_N };
/* The bottom chip row is the tight one: V and T sit 8 px apart, so those
 * facing edges get only +3; every other edge takes the surrounding space. */
inline const Target GRAPH_T[GRAPH_BTN_N] = {
  { {  8,   6,  32, 30}, {  0,   0,  64, 43} },   /* Back -> corner          */
  { { 86, 246,  40, 24}, { 60, 238,  69, 40} },   /* V chip                  */
  { {134, 246,  40, 24}, {131, 238,  47, 40} },   /* T chip                  */
  { {254, 246,  22, 24}, {250, 238,  40, 40} },   /* timer R                 */
  { {  8, 280,  32, 32}, {  0, 272,  46, 48} },   /* peak R -> bottom-left   */
};

/* ------------------------------------------------------- CALIBRATE / DEV -- */
inline const Target CAL_T[1] = { { {  8,   8,  52, 30}, {  0,   0,  96, 52} } };
inline const Target DEV_T[1] = { { {410, 282,  62, 30}, {394, 262,  86, 58} } };
