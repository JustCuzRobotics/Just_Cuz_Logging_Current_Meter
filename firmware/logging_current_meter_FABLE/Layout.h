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
enum { HOME_LIVE, HOME_GRAPH, HOME_LOG, HOME_TEST, HOME_SETTINGS, HOME_N };
inline const Target HOME_T[HOME_N] = {
  { { 20,  44, 213, 121}, { 14,  38, 225, 133} },
  { {247,  44, 213, 121}, {241,  38, 225, 133} },
  { { 20, 179, 137, 121}, { 14, 173, 149, 133} },   /* Log                 */
  { {171, 179, 137, 121}, {165, 173, 149, 133} },
  { {322, 179, 138, 121}, {316, 173, 150, 133} },
};
inline const char *const HOME_LABEL[HOME_N] = { "LIVE VIEW", "GRAPH", "LOG", "TEST MODE", "SETTINGS" };
inline const char *const HOME_SUB[HOME_N]   = { "V I T W ENERGY", "SCALED PLOT, PEAKS",
                                                "SD + USB STREAM", "ESC SIGNAL + CYCLE", "THEME FILTER DEV" };
inline const bool HOME_DISABLED[HOME_N]     = { false, false, false, false, false };

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

/* ------------------------------------------------------------ DEV MODE -- *
 * Back sits bottom-right so it cannot be hit while dragging the crosshair
 * around the middle of the panel; the theme toggle takes the opposite
 * corner, far enough away that the two hit rects never meet. */
enum { DEV_BTN_BACK, DEV_BTN_THEME, DEV_BTN_N };
inline const Target DEV_T[DEV_BTN_N] = {
  { {410, 282,  62, 30}, {394, 262,  86, 58} },
  { {  8, 282, 132, 30}, {  0, 262, 150, 58} },
};

/* ----------------------------------------------------------- TEST MODE --
 * Two halves. The top is the manual set point — a five-part stepper with the
 * live pulse width in the middle, coarse outside, fine inside — plus the
 * frame period and a status readout. The bottom is the auto-cycle: low and
 * high pulse on one row, the two dwells on the next, start/stop across the
 * foot where a hand reaching for it cannot brush anything else. */
#define TEST_TITLE_CX 176      /* left of the ARM button, right of Back */
inline const JCRRect TEST_PULSE_BOX  = {144,  58, 192, 44};
inline const JCRRect TEST_PERIOD_BOX = { 62, 118, 116, 32};
inline const JCRRect TEST_STATUS_BOX = {252, 118, 216, 32};
inline const JCRRect TEST_CYC_BOX[4] = { { 62, 174, 116, 32}, {302, 174, 116, 32},
                                         { 62, 220, 116, 32}, {302, 220, 116, 32} };
inline const char *const TEST_CYC_LABEL[4] = { "CYCLE LOW US", "CYCLE HIGH US",
                                               "DWELL LOW MS", "DWELL HIGH MS" };
/* Label rows: each 5x7 caption sits in the 8 px above its control row. */
#define TEST_Y_MANUAL_LBL   46
#define TEST_Y_PERIOD_LBL  106
#define TEST_Y_DIVIDER     158
#define TEST_Y_CYC_LBL     164
#define TEST_Y_DWELL_LBL   210

enum { TEST_BACK, TEST_ARM,
       TEST_P_M50, TEST_P_M10, TEST_P_P10, TEST_P_P50,
       TEST_PER_M, TEST_PER_P,
       TEST_LO_M,  TEST_LO_P,  TEST_HI_M,  TEST_HI_P,
       TEST_DLO_M, TEST_DLO_P, TEST_DHI_M, TEST_DHI_P,
       TEST_CYCLE, TEST_BTN_N };
inline const Target TEST_T[TEST_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  90, 50} },   /* Back                    */
  { {300,   8, 168, 30}, {292,   0, 188, 52} },   /* ARM / DISARM            */
  { { 12,  58,  56, 44}, {  0,  52,  72, 54} },   /* pulse -50               */
  { { 76,  58,  56, 44}, { 72,  52,  68, 54} },   /* pulse -10               */
  { {344,  58,  56, 44}, {340,  52,  68, 54} },   /* pulse +10               */
  { {412,  58,  56, 44}, {408,  52,  72, 54} },   /* pulse +50               */
  { { 12, 118,  44, 32}, {  0, 110,  58, 44} },   /* period -                */
  { {184, 118,  44, 32}, {180, 110,  60, 44} },   /* period +                */
  { { 12, 174,  44, 32}, {  0, 168,  58, 44} },   /* cycle low  -            */
  { {184, 174,  44, 32}, {180, 168,  58, 44} },   /* cycle low  +            */
  { {252, 174,  44, 32}, {244, 168,  58, 44} },   /* cycle high -            */
  { {424, 174,  44, 32}, {420, 168,  60, 44} },   /* cycle high +            */
  { { 12, 220,  44, 32}, {  0, 214,  58, 44} },   /* dwell low  -            */
  { {184, 220,  44, 32}, {180, 214,  58, 44} },   /* dwell low  +            */
  { {252, 220,  44, 32}, {244, 214,  58, 44} },   /* dwell high -            */
  { {424, 220,  44, 32}, {420, 214,  60, 44} },   /* dwell high +            */
  { { 12, 264, 456, 36}, {  0, 258, 480,  62} },  /* START / STOP CYCLE      */
};

/* ------------------------------------------------------------ SETTINGS --
 * A label column on the left, controls in a fixed column on the right, one
 * row per setting. New settings drop in as another row without disturbing
 * anything above them, which is the point of laying it out this way. */
inline const JCRRect SET_FILTER_BOX = {302, 102, 120, 40};
inline const int16_t SET_ROW_Y[5]   = { 50, 102, 154, 206, 258 };
inline const char *const SET_ROW_LABEL[5] = { "THEME", "FILTER", "CALIBRATION",
                                              "SETTINGS", "DIAGNOSTICS" };
inline const char *const SET_ROW_SUB[5]   = { "PANEL PALETTE",
                                              "V+I SMOOTHING WINDOW",
                                              "DUMP CONSTANTS OVER SERIAL",
                                              "WRITE CURRENT VALUES TO FLASH",
                                              "TOUCH + FPS DEBUG SCREEN" };

enum { SET_BACK, SET_THEME, SET_FILTER_M, SET_FILTER_P,
       SET_DUMP, SET_SAVE, SET_DEV, SET_BTN_N };
inline const Target SET_T[SET_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  96, 44} },   /* Back                    */
  { {300,  50, 168, 40}, {240,  44, 240, 52} },   /* theme toggle            */
  { {252, 102,  44, 40}, {240,  96,  52, 52} },   /* filter -                */
  { {426, 102,  42, 40}, {422,  96,  58, 52} },   /* filter +                */
  { {252, 154, 216, 40}, {240, 148, 240, 52} },   /* dump calibration        */
  { {252, 206, 216, 40}, {240, 200, 240, 52} },   /* save settings           */
  { {252, 258, 216, 40}, {240, 252, 240, 68} },   /* dev mode                */
};

/* ------------------------------------------------------------------ LOG --
 * Same two-column grid as Test Mode's cycle rows. Top: four steppers (mode,
 * rate, trigger threshold, duration). Middle: status on the left, USB stream
 * / save / remount on the right. Foot: START / STOP LOG, full width, like
 * START CYCLE — the one control a hand reaches for without looking.
 * Steppers here use 38 px buttons so the value box is 128 px: "CURRENT" in
 * RUSSO16 is 121 px. */
#define LOG_TITLE_CX 240
inline const JCRRect LOG_BOX[4] = { { 56,  56, 128, 32}, {296,  56, 128, 32},
                                    { 56, 112, 128, 32}, {296, 112, 128, 32} };
inline const char *const LOG_BOX_LABEL[4] = { "LOG MODE", "RATE",
                                              "AUTO START ABOVE", "DURATION" };
#define LOG_Y_ROW1_LBL   44
#define LOG_Y_ROW2_LBL  100
#define LOG_Y_DIVIDER   154
#define LOG_STATUS_X     12
#define LOG_Y_STATE     164     /* 5x7 at scale 2                          */
#define LOG_Y_LINE2     186     /* then 5x7 scale 1 lines, 14 px apart     */
#define LOG_STATUS_CHARS 37     /* 222 px of 5x7 — stops short of x = 240  */

enum { LOG_BACK,
       LOG_MODE_M, LOG_MODE_P, LOG_RATE_M, LOG_RATE_P,
       LOG_THR_M,  LOG_THR_P,  LOG_DUR_M,  LOG_DUR_P,
       LOG_USB, LOG_SAVE, LOG_MOUNT, LOG_START, LOG_BTN_N };
inline const Target LOG_T[LOG_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  90, 44} },   /* Back                    */
  { { 12,  56,  38, 32}, {  0,  50,  53, 46} },   /* mode -                  */
  { {190,  56,  38, 32}, {187,  50,  53, 46} },   /* mode +                  */
  { {252,  56,  38, 32}, {240,  50,  53, 46} },   /* rate -                  */
  { {430,  56,  38, 32}, {427,  50,  53, 46} },   /* rate +                  */
  { { 12, 112,  38, 32}, {  0, 106,  53, 44} },   /* threshold -             */
  { {190, 112,  38, 32}, {187, 106,  53, 44} },   /* threshold +             */
  { {252, 112,  38, 32}, {240, 106,  53, 44} },   /* duration -              */
  { {430, 112,  38, 32}, {427, 106,  53, 44} },   /* duration +              */
  { {252, 164, 104, 36}, {244, 158, 114, 46} },   /* USB stream toggle       */
  { {364, 164, 104, 36}, {358, 158, 122, 46} },   /* save                    */
  { {252, 208, 216, 36}, {244, 204, 236, 46} },   /* remount SD              */
  { { 12, 258, 456, 42}, {  0, 252, 480, 68} },   /* START / STOP LOG        */
};

/* ------------------------------------------------------- ESC INDICATOR --
 * A 4 px amber strip along the very top edge, drawn on every screen while
 * the ESC output is armed. Every control on every screen starts at y >= 6,
 * so this can never collide with one — which matters more than elegance for
 * a warning that a motor may be about to spin. */
inline const int16_t ESC_BAR_H = 4;

/* ------------------------------------------------------- LOG INDICATOR --
 * The same idea along the BOTTOM edge: solid red while a log is recording,
 * dashed red while CURRENT mode is armed and waiting. Every control's visual
 * rect ends at y <= 312, so rows 316-319 are free on every screen. */
inline const int16_t LOG_BAR_H = 4;
