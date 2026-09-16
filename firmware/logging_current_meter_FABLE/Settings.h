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

struct Settings {
  uint8_t  theme;          /* ThemeId                                      */
  uint8_t  filterIndex;    /* index into FILTER_SAMPLES                    */
  uint16_t escPulseUs;     /* manual set point                             */
  uint16_t escPeriodUs;    /* frame period, 20000 = 50 Hz                  */
  uint16_t cycleLoUs, cycleHiUs;
  uint16_t cycleLoMs, cycleHiMs;   /* dwell at each end                    */
  /* ---- v2 ---- */
  uint8_t  logMode;        /* LogMode                                      */
  uint8_t  logRateIdx;     /* index into LOG_RATE_DECIM                    */
  uint8_t  logDurIdx;      /* index into LOG_DUR_MIN; 0 = until stopped    */
  uint8_t  logThreshA;     /* CURRENT mode start threshold, whole amps     */
  uint8_t  streamOn;       /* USB CSV stream enabled at boot               */
  uint8_t  _pad[3];
};

extern Settings gSet;

void settingsBegin();     /* load from flash, or defaults. Core 0, in setup */
bool settingsSave();      /* write to flash. Core 0 only. true on success   */
bool settingsLoadedFromFlash();
void settingsDefaults(Settings &s);
