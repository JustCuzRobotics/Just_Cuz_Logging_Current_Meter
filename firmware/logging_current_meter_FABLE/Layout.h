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
 * Three tabs share one header: Back, MANUAL | CYCLE | LOG TEST, and ARM/STOP
 * top right. Labels are 5x7 at scale 2 (12 px per char) so every width is
 * checkable: "LOG TEST" is 96 px in a 104 px tab. Hit rects tile the header
 * edge to edge (0..480 x 0..44) with no gaps and no overlap.
 * A status line (5x7 x2, 38 chars max) sits under the header on all tabs. */
enum { TH_BACK, TH_TAB_MANUAL, TH_TAB_CYCLE, TH_TAB_LOG, TH_ARM };
/* An int, not an enumerator: the tab enums below continue numbering from it,
 * and mixing two enum types in arithmetic is deprecated in C++20. */
constexpr int TH_N = 5;
inline const Target TH_T[TH_N] = {
  { {  4,   6,  56, 32}, {  0,   0,  62, 44} },   /* Back                    */
  { { 66,   6,  98, 32}, { 62,   0, 106, 44} },   /* MANUAL tab              */
  { {170,   6,  88, 32}, {168,   0,  94, 44} },   /* CYCLE tab               */
  { {264,   6, 104, 32}, {262,   0, 110, 44} },   /* LOG TEST tab            */
  { {376,   6, 100, 32}, {372,   0, 108, 44} },   /* ARM / STOP              */
};
inline const char *const TH_TAB_LABEL[3] = { "MANUAL", "CYCLE", "LOG TEST" };
#define TM_STATUS_X        8
#define TM_STATUS_Y       50
#define TM_STATUS_CHARS   38

/* ---- MANUAL tab ---- (ids continue after the header's) */
enum { TMM_ESC = TH_N, TMM_CTRL, TMM_RELEASE,
       TMM_M50, TMM_M10, TMM_P10, TMM_P50,
       TMM_SLIDER, TMM_IDLE, TMM_N };
inline const Target TMM_T[TMM_N - TH_N] = {
  { {  8,  72, 150, 36}, {  0,  68, 161, 44} },   /* ESC UNI / BIDI          */
  { {164,  72, 150, 36}, {161,  68, 156, 44} },   /* STEPPERS / SLIDER       */
  { {320,  72, 152, 36}, {317,  68, 163, 44} },   /* HOLD / DEAD-MAN         */
  { {  8, 176, 110, 64}, {  0, 168, 121, 78} },   /* -50  (step mode)        */
  { {124, 176, 110, 64}, {121, 168, 119, 78} },   /* -10                     */
  { {246, 176, 110, 64}, {240, 168, 119, 78} },   /* +10                     */
  { {362, 176, 110, 64}, {359, 168, 121, 78} },   /* +50                     */
  { { 20, 176, 440, 64}, {  0, 168, 480, 78} },   /* slider  (slider mode)   */
  { {  8, 256, 464, 42}, {  0, 248, 480, 72} },   /* IDLE                    */
};
/* Big readout: pulse in RUSSOBIG (24 px), then unit/percent in 5x7 x2. */
#define TMM_READ_Y        120
#define TMM_READ_X        110
/* Slider interior: the thumb travels across this, 1000 us at the left edge,
 * 2000 us at the right. */
inline const JCRRect TMM_TRACK = { 22, 178, 436, 60 };
#define TMM_THUMB_W        20

/* ---- CYCLE and LOG TEST tabs ---- */
enum { TMC_TILE0 = TH_N,                      /* 8 tiles                      */
       TMC_BIG_M = TMC_TILE0 + 8, TMC_SMALL_M, TMC_SMALL_P, TMC_BIG_P,
       TMC_RESET, TMC_START, TMC_N };
enum { TILE_LOW, TILE_HIGH, TILE_RUP, TILE_DHI, TILE_RDN, TILE_DLO, TILE_DIR, TILE_CYC, TILE_N };
inline const char *const TILE_LABEL[TILE_N] = { "LOW US", "HIGH US", "RAMP UP", "DWELL HI",
                                                "RAMP DOWN", "DWELL LO", "DIRECTION", "CYCLES" };
inline const Target TMC_T[TMC_N - TH_N] = {
  { {  8,  70, 113, 44}, {  0,  66, 123, 50} },   /* tiles, row 1            */
  { {125,  70, 113, 44}, {123,  66, 117, 50} },
  { {242,  70, 113, 44}, {240,  66, 117, 50} },
  { {359,  70, 113, 44}, {357,  66, 123, 50} },
  { {  8, 118, 113, 44}, {  0, 116, 123, 49} },   /* tiles, row 2            */
  { {125, 118, 113, 44}, {123, 116, 117, 49} },
  { {242, 118, 113, 44}, {240, 116, 117, 49} },
  { {359, 118, 113, 44}, {357, 116, 123, 49} },
  { {  8, 170,  64, 40}, {  0, 165,  74, 48} },   /* -big                    */
  { { 76, 170,  64, 40}, { 74, 165,  68, 48} },   /* -small                  */
  { {340, 170,  64, 40}, {338, 165,  68, 48} },   /* +small                  */
  { {408, 170,  64, 40}, {406, 165,  74, 48} },   /* +big                    */
  { {  8, 256, 150, 42}, {  0, 248, 162, 72} },   /* RESET DEFAULTS          */
  { {166, 256, 306, 42}, {162, 248, 318, 72} },   /* START / STOP            */
};
inline const JCRRect TMC_VALUE_BOX = { 144, 170, 192, 40 };
#define TMC_STAT_Y1       218      /* two status lines, 5x7 x2 then x1        */
#define TMC_STAT_Y2       238

/* ------------------------------------------------------------ SETTINGS --
 * A label column on the left, controls in a fixed column on the right, one
 * row per setting. New settings drop in as another row without disturbing
 * anything above them, which is the point of laying it out this way. */
/* Six rows at a 44 px pitch (it was five at 52 before the clock row). Label
 * on the baseline, description under it, divider 38 px down, control 36 px
 * tall in the right-hand column. */
#define SET_ROW_N 6
inline const JCRRect SET_FILTER_BOX = {302,  96, 120, 36};
inline const JCRRect SET_PRE_BOX    = {302, 184, 120, 36};
inline const int16_t SET_ROW_Y[SET_ROW_N] = { 50, 94, 138, 182, 226, 270 };
inline const char *const SET_ROW_LABEL[SET_ROW_N] = { "THEME", "FILTER", "CLOCK",
                                                      "ESC PRE-ROLL", "SETTINGS",
                                                      "TOOLS" };
/* Rows 1, 2 and 3 write their own description line every tick. The last row
 * carries two buttons, which is what keeps this to six rows. */
inline const char *const SET_ROW_SUB[SET_ROW_N]   = { "PANEL PALETTE",
                                                      "V+I SMOOTHING WINDOW",
                                                      "DATE + TIME FOR LOG FILES",
                                                      "IDLE BEFORE A RUN",
                                                      "WRITE CURRENT VALUES TO FLASH",
                                                      "CALIBRATION DUMP + TOUCH DEBUG" };

enum { SET_BACK, SET_THEME, SET_FILTER_M, SET_FILTER_P, SET_CLOCK,
       SET_PRE_M, SET_PRE_P, SET_SAVE, SET_DUMP, SET_DEV, SET_BTN_N };
inline const Target SET_T[SET_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  96, 44} },   /* Back                    */
  { {300,  52, 168, 36}, {240,  48, 240, 42} },   /* theme toggle            */
  { {252,  96,  44, 36}, {240,  92,  64, 42} },   /* filter -                */
  { {426,  96,  42, 36}, {422,  92,  58, 42} },   /* filter +                */
  { {252, 140, 216, 36}, {240, 136, 240, 42} },   /* set clock               */
  { {252, 184,  44, 36}, {240, 180,  64, 42} },   /* pre-roll -              */
  { {426, 184,  42, 36}, {422, 180,  58, 42} },   /* pre-roll +              */
  { {252, 228, 216, 36}, {240, 224, 240, 42} },   /* save settings           */
  { {252, 272, 104, 36}, {240, 268, 118, 44} },   /* dump calibration        */
  { {364, 272, 104, 36}, {358, 268, 122, 44} },   /* dev mode                */
};

/* ---------------------------------------------------------------- CLOCK --
 * SET CLOCK, reached from the Settings clock row. Five fields as tiles, the
 * same editor row as the Test Mode profile, then the working value big and
 * the live clock underneath it. Nothing moves until APPLY. */
enum { CLK_BACK, CLK_TILE0, CLK_TILE1, CLK_TILE2, CLK_TILE3, CLK_TILE4,
       CLK_BIG_M, CLK_SMALL_M, CLK_SMALL_P, CLK_BIG_P, CLK_APPLY, CLK_BTN_N };
enum { CFLD_YEAR, CFLD_MONTH, CFLD_DAY, CFLD_HOUR, CFLD_MIN, CFLD_N };
inline const Target CLK_T[CLK_BTN_N] = {
  { {  8,   8,  52, 30}, {  0,   0,  96, 44} },   /* Back (cancels)          */
  { {  8,  58,  88, 54}, {  6,  52,  92, 62} },   /* YEAR                    */
  { {102,  58,  88, 54}, {100,  52,  92, 62} },   /* MONTH                   */
  { {196,  58,  88, 54}, {194,  52,  92, 62} },   /* DAY                     */
  { {290,  58,  88, 54}, {288,  52,  92, 62} },   /* HOUR                    */
  { {384,  58,  88, 54}, {382,  52,  92, 62} },   /* MIN                     */
  { {  8, 124,  84, 44}, {  6, 120,  88, 52} },   /* big -                   */
  { {100, 124,  84, 44}, {  98, 120,  88, 52} },  /* small -                 */
  { {296, 124,  84, 44}, {294, 120,  88, 52} },   /* small +                 */
  { {388, 124,  84, 44}, {386, 120,  88, 52} },   /* big +                   */
  { {252, 262, 220, 46}, {240, 256, 240, 58} },   /* APPLY                   */
};
inline const JCRRect CLK_VALUE_BOX = { 192, 124,  96, 44};
#define CLK_BIG_Y     186      /* working value, RUSSO22, centred           */
#define CLK_STAT_X      8
#define CLK_STAT_Y1   224      /* live clock, 5x7 x1                        */
#define CLK_STAT_Y2   240      /* what APPLY will do                        */
#define CLK_STAT_CHARS 72      /* 432 px of 5x7 x1                          */

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
