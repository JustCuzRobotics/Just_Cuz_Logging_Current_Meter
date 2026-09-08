#include "Settings.h"
#include "Config.h"
#include "Theme.h"
#include <EEPROM.h>

const uint8_t FILTER_SAMPLES[FILTER_OPTION_COUNT] = { 0, 2, 4, 6, 8, 10, 15, 20 };

uint16_t filterWindowMs(uint8_t i) {
  if (i >= FILTER_OPTION_COUNT || FILTER_SAMPLES[i] == 0) return 0;
  return (uint16_t)((uint32_t)FILTER_SAMPLES[i] * TICK_US / 1000UL);
}

Settings gSet;

#define SETTINGS_MAGIC   0x4A43524CUL   /* 'JCRL' */
#define SETTINGS_VERSION 1
#define SETTINGS_ADDR    0

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  Settings s;
  uint16_t checksum;
};

static bool s_fromFlash = false;

void settingsDefaults(Settings &s) {
  s.theme       = THEME_DARK;
  s.filterIndex = 2;          /* 4 samples ≈ 53 ms — visible smoothing,
                               * still well inside a fast pull             */
  s.escPulseUs  = 1000;       /* idle. Never boot anywhere else            */
  s.escPeriodUs = 20000;      /* 50 Hz standard servo frame                */
  s.cycleLoUs   = 1000;
  s.cycleHiUs   = 1500;
  s.cycleLoMs   = 3000;
  s.cycleHiMs   = 3000;
}

static uint16_t sumOf(const Settings &s) {
  const uint8_t *p = (const uint8_t *)&s;
  uint16_t c = 0;
  for (size_t i = 0; i < sizeof(Settings); i++) c = (uint16_t)(c * 31u + p[i]);
  return c;
}

/* Guard every field on load. A value that is merely stale is fine; a value
 * that is out of range would drive the PWM or index an array, so it is
 * rejected rather than clamped silently into something plausible. */
static bool plausible(const Settings &s) {
  if (s.theme >= THEME_COUNT) return false;
  if (s.filterIndex >= FILTER_OPTION_COUNT) return false;
  if (s.escPulseUs  < 800  || s.escPulseUs  > 2500) return false;
  if (s.escPeriodUs < 2500 || s.escPeriodUs > 50000) return false;
  if (s.cycleLoUs   < 800  || s.cycleLoUs   > 2500) return false;
  if (s.cycleHiUs   < 800  || s.cycleHiUs   > 2500) return false;
  if (s.cycleLoMs   < 100  || s.cycleLoMs   > 60000) return false;
  if (s.cycleHiMs   < 100  || s.cycleHiMs   > 60000) return false;
  if (s.escPulseUs > s.escPeriodUs || s.cycleHiUs > s.escPeriodUs) return false;
  return true;
}

void settingsBegin() {
  settingsDefaults(gSet);
  s_fromFlash = false;

  EEPROM.begin(512);
  StoredSettings st;
  EEPROM.get(SETTINGS_ADDR, st);

  if (st.magic == SETTINGS_MAGIC && st.version == SETTINGS_VERSION &&
      st.size == sizeof(Settings) && st.checksum == sumOf(st.s) &&
      plausible(st.s)) {
    gSet = st.s;
    s_fromFlash = true;
  }
  themeApply(gSet.theme);
}

bool settingsSave() {
  StoredSettings st;
  st.magic    = SETTINGS_MAGIC;
  st.version  = SETTINGS_VERSION;
  st.size     = sizeof(Settings);
  /* Never persist a live throttle. Whatever the set point is when Save is
   * pressed, what goes to flash is idle — a board that resets with a motor
   * attached must not come back under power. */
  st.s = gSet;
  st.s.escPulseUs = 1000;
  st.checksum = sumOf(st.s);
  EEPROM.put(SETTINGS_ADDR, st);
  bool ok = EEPROM.commit();      /* the few-ms both-core stall lives here */
  if (ok) s_fromFlash = true;
  return ok;
}

bool settingsLoadedFromFlash() { return s_fromFlash; }
