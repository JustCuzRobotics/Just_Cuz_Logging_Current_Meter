/* ==========================================================================
 * Settings.h — user settings, and their flash persistence.
 *
 * Saving is deliberately explicit. An RP2040 flash write halts BOTH cores for
 * a few milliseconds while XIP is disabled to erase and program the sector —
 * harmless when you choose the moment, but it would put a hole in a capture
 * if it fired on every toggle. So changes take effect immediately in RAM and
 * only reach flash when Save is pressed.
 *
 * The stored blob carries a magic and a version. Anything that does not match
 * is treated as absent and the Config.h defaults are used, so a layout change
 * in a future build degrades to defaults rather than loading garbage into a
 * live instrument.
 * ========================================================================*/
#pragma once
#include <Arduino.h>
#include "EscProfile.h"

/* Filter window options, in samples. Index 0 is off. At the 13.158 ms tick
 * these are 26 ms through 263 ms; filterWindowMs() does that arithmetic for
 * the Settings screen so the trade is visible where it is chosen. */
#define FILTER_OPTION_COUNT 8
extern const uint8_t FILTER_SAMPLES[FILTER_OPTION_COUNT];   /* {0,2,4,6,8,10,15,20} */
uint16_t filterWindowMs(uint8_t optionIndex);

/* ---- logging options (v2) ----
 * Rate is a decimation of the 13.158 ms sample tick, so every rate is an
 * exact subset of the same samples: 76 / 38 / 15.2 / 7.6 / 1.0 Hz. */
#define LOG_RATE_COUNT 5
extern const uint8_t  LOG_RATE_DECIM[LOG_RATE_COUNT];     /* {1,2,5,10,76}     */
extern const char *const LOG_RATE_LABEL[LOG_RATE_COUNT];  /* "76","38",...    */
#define LOG_DUR_COUNT 7
extern const uint8_t  LOG_DUR_MIN[LOG_DUR_COUNT];         /* {0,1,2,3,5,10,15} */
#define LOG_THRESH_MIN_A   1
#define LOG_THRESH_MAX_A  50

enum LogMode : uint8_t { LOGMODE_MANUAL = 0, LOGMODE_CYCLE = 1, LOGMODE_CURRENT = 2,
                         LOGMODE_COUNT = 3 };

/* ---- Test Mode (v3) ---- */
enum CtrlStyle   : uint8_t { CTRL_STEP = 0, CTRL_SLIDER = 1 };
enum ReleaseMode : uint8_t { RELEASE_HOLD = 0, RELEASE_DEADMAN = 1 };

/* Profile limits and editor steps (small / big). */
#define PROF_RAMP_MAX_MS    3000
#define PROF_DWELL_MIN_MS    500
/* Three minutes. Long dwells are how a motor gets held at load for a thermal
 * run, so the cap is generous; the editor's steps grow with the value
 * (see tileSteps in ScreenTestCycle.cpp) rather than making you tap 500 ms at
 * a time to get there. */
#define PROF_DWELL_MAX_MS 180000
/* Above this the ms steps switch to their coarse pair. */
#define PROF_MS_COARSE_ABOVE 5000
#define PROF_CYCLES_MAX      999
#define PROF_UNI_HIGH_DEF   1500
#define PROF_BIDI_HIGH_DEF  1750

/* Pre-roll: idle held after the output comes up, before a run's first ramp.
 * Long enough that the ESC has finished its start-up (beeps, or music on some
 * firmware) and will actually spin when the ramp begins. 5 s suits the ESCs on
 * this bench; an ESC with a long startup tune wants more. */
#define PRE_ROLL_DEF_MS     5000
#define PRE_ROLL_MAX_MS    15000
#define PRE_ROLL_STEP_MS     500

struct Settings {
  /* uint32 first, then 14 bytes of uint8, then uint16s — in that order the
   * struct has no hidden padding on any alignment (checked below). */
  uint32_t clockEpoch;     /* wall clock at the last save, local epoch secs  */
  uint32_t profDwellHiMs, profDwellLoMs;  /* up to 3 min - 32 bits needed    */
  uint8_t  clockEverSet;   /* 0 = clockEpoch has never held a real time      */
  uint8_t  reserved0;      /* keeps the uint8 run even, no padding before u16 */
  uint8_t  theme;          /* ThemeId                                      */
  uint8_t  filterIndex;    /* index into FILTER_SAMPLES                    */
  uint8_t  logMode;        /* LogMode                                      */
  uint8_t  logRateIdx;     /* index into LOG_RATE_DECIM                    */
  uint8_t  logDurIdx;      /* index into LOG_DUR_MIN; 0 = until stopped    */
  uint8_t  logThreshA;     /* CURRENT mode start threshold, whole amps     */
  uint8_t  streamOn;       /* USB CSV stream enabled at boot               */
  uint8_t  escType;        /* EscType: UNI / BIDI                          */
  uint8_t  ctrlStyle;      /* CtrlStyle: manual steppers or slider         */
  uint8_t  releaseMode;    /* ReleaseMode: slider hold or dead-man         */
  uint8_t  cycleMode;      /* CycleMode: STOP->SPIN or SPIN->SPIN          */
  uint8_t  testTab;        /* last Test Mode tab (0 manual, 1 cycle, 2 log) */
  uint16_t profLowUs, profHighUs;
  uint16_t profRampUpMs, profRampDnMs;
  uint16_t profCycles;     /* Log Test cycle count                         */
  uint16_t preRollMs;      /* idle before a run's first ramp               */
  uint16_t reserved1;      /* the uint32 aligns the struct to 4 - this keeps
                            * the size explicit rather than implied padding  */
};
static_assert(sizeof(Settings) == 40, "Settings has hidden padding - checksum/migration assume none");

extern Settings gSet;

void settingsBegin();     /* load from flash, or defaults. Core 0, in setup */
bool settingsSave();      /* write to flash. Core 0 only. true on success   */
/* Persist only the wall clock: writes the blob as it was last saved, with
 * clockEpoch refreshed from clockNow(). Used by the 5-minute idle save and
 * the log boundaries, so those never quietly commit UI changes the operator
 * has not pressed SAVE for. Refused while a log is recording, like save. */
bool settingsSaveClock();
bool settingsLoadedFromFlash();
void settingsDefaults(Settings &s);

/* Profile defaults for the current ESC type (RESET DEFAULTS on the Cycle tab),
 * and a range check that snaps the high pulse into the type's valid band. */
void settingsProfileDefaults(Settings &s);
void clampProfileRange(Settings &s);   /* pulses back inside 1000-2000       */
void settingsFixProfileForType(Settings &s);
