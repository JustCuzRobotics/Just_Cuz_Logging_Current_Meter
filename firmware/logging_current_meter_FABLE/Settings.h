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

struct Settings {
  uint8_t  theme;          /* ThemeId                                      */
  uint8_t  filterIndex;    /* index into FILTER_SAMPLES                    */
  uint16_t escPulseUs;     /* manual set point                             */
  uint16_t escPeriodUs;    /* frame period, 20000 = 50 Hz                  */
  uint16_t cycleLoUs, cycleHiUs;
  uint16_t cycleLoMs, cycleHiMs;   /* dwell at each end                    */
};

extern Settings gSet;

void settingsBegin();     /* load from flash, or defaults. Core 0, in setup */
bool settingsSave();      /* write to flash. Core 0 only. true on success   */
bool settingsLoadedFromFlash();
void settingsDefaults(Settings &s);
