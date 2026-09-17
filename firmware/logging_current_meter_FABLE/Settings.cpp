#include "Settings.h"
#include "Config.h"
#include "Theme.h"
#include <EEPROM.h>
#include <string.h>

const uint8_t FILTER_SAMPLES[FILTER_OPTION_COUNT] = { 0, 2, 4, 6, 8, 10, 15, 20 };
const uint8_t LOG_RATE_DECIM[LOG_RATE_COUNT] = { 1, 2, 5, 10, 76 };
const char *const LOG_RATE_LABEL[LOG_RATE_COUNT] = { "76 HZ", "38 HZ", "15 HZ", "7.6 HZ", "1 HZ" };
const uint8_t LOG_DUR_MIN[LOG_DUR_COUNT] = { 0, 1, 2, 3, 5, 10, 15 };

uint16_t filterWindowMs(uint8_t i) {
  if (i >= FILTER_OPTION_COUNT || FILTER_SAMPLES[i] == 0) return 0;
  return (uint16_t)((uint32_t)FILTER_SAMPLES[i] * TICK_US / 1000UL);
}

Settings gSet;

#define SETTINGS_MAGIC   0x4A43524CUL   /* 'JCRL' */
#define SETTINGS_VERSION 2
#define SETTINGS_ADDR    0

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  Settings s;
  uint16_t checksum;
};

/* The v1 layout, kept only so a board saved by v3.1c keeps its theme, filter
 * and ESC values when it is first flashed with v3.2 — the logging fields are
 * then filled from defaults. It must stay byte-identical to v3.1c's struct. */
struct SettingsV1 {
  uint8_t  theme, filterIndex;
  uint16_t escPulseUs, escPeriodUs;
  uint16_t cycleLoUs, cycleHiUs, cycleLoMs, cycleHiMs;
};
struct StoredSettingsV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  SettingsV1 s;
  uint16_t checksum;
};

static bool s_fromFlash = false;

void settingsDefaults(Settings &s) {
  s.theme       = THEME_DARK;
  s.filterIndex = 2;          /* 4 samples ≈ 53 ms — visible smoothing,
                               * still well inside a fast pull             */
  s.escPulseUs  = 1000;       /* idle. Never boot anywhere else            */
  s.cycleLoUs   = 1000;
  s.cycleHiUs   = 1500;
  s.cycleLoMs   = 3000;
  s.cycleHiMs   = 3000;
  s.logMode     = LOGMODE_MANUAL;
  s.logRateIdx  = 0;          /* 76 Hz — every sample                      */
  s.logDurIdx   = 0;          /* until stopped                             */
  s.logThreshA  = 5;          /* above tare drift + nominal-gain error     */
  s.streamOn    = 0;
  memset(s._pad, 0, sizeof s._pad);
}

static uint16_t sumBytes(const uint8_t *p, size_t n) {
  uint16_t c = 0;
  for (size_t i = 0; i < n; i++) c = (uint16_t)(c * 31u + p[i]);
  return c;
}
static uint16_t sumOf(const Settings &s) { return sumBytes((const uint8_t *)&s, sizeof(Settings)); }

/* Guard every field on load. A value that is merely stale is fine; a value
 * that is out of range would drive the PWM or index an array, so it is
 * rejected rather than clamped silently into something plausible. */
static bool plausible(const Settings &s) {
  if (s.theme >= THEME_COUNT) return false;
  if (s.filterIndex >= FILTER_OPTION_COUNT) return false;
  if (s.escPulseUs  < 800  || s.escPulseUs  > 2500) return false;
  if (s.cycleLoUs   < 800  || s.cycleLoUs   > 2500) return false;
  if (s.cycleHiUs   < 800  || s.cycleHiUs   > 2500) return false;
  if (s.cycleLoMs   < 100  || s.cycleLoMs   > 60000) return false;
  if (s.cycleHiMs   < 100  || s.cycleHiMs   > 60000) return false;
  if (s.logMode >= LOGMODE_COUNT) return false;
  if (s.logRateIdx >= LOG_RATE_COUNT) return false;
  if (s.logDurIdx >= LOG_DUR_COUNT) return false;
  if (s.logThreshA < LOG_THRESH_MIN_A || s.logThreshA > LOG_THRESH_MAX_A) return false;
  if (s.streamOn > 1) return false;
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
  } else if (st.magic == SETTINGS_MAGIC && st.version == 1) {
    /* Migrate a v3.1c blob: carry its fields over, default the rest. */
    StoredSettingsV1 v1;
    EEPROM.get(SETTINGS_ADDR, v1);
    if (v1.size == sizeof(SettingsV1) &&
        v1.checksum == sumBytes((const uint8_t *)&v1.s, sizeof(SettingsV1))) {
      Settings m;
      settingsDefaults(m);
      m.theme = v1.s.theme;             m.filterIndex = v1.s.filterIndex;
      m.escPulseUs = v1.s.escPulseUs;   /* v1's frame period is dropped:
                                         * the Servo library's frame is fixed */
      m.cycleLoUs = v1.s.cycleLoUs;     m.cycleHiUs = v1.s.cycleHiUs;
      m.cycleLoMs = v1.s.cycleLoMs;     m.cycleHiMs = v1.s.cycleHiMs;
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
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
