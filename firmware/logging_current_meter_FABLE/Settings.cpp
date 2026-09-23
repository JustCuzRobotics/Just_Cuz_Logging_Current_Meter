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
#define SETTINGS_VERSION 3
#define SETTINGS_ADDR    0

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  Settings s;
  uint16_t checksum;
};

/* Earlier layouts, kept only for migration. Each must stay byte-identical to
 * the struct that build saved. v1 = v3.1c, v2 = v3.2 / v3.2a. */
struct SettingsV1 {
  uint8_t  theme, filterIndex;
  uint16_t escPulseUs, escPeriodUs;
  uint16_t cycleLoUs, cycleHiUs, cycleLoMs, cycleHiMs;
};
struct SettingsV2 {
  uint8_t  theme, filterIndex;
  uint16_t escPulseUs;
  uint16_t cycleLoUs, cycleHiUs, cycleLoMs, cycleHiMs;
  uint8_t  logMode, logRateIdx, logDurIdx, logThreshA, streamOn;
  uint8_t  pad[3];
};
template <typename S> struct StoredOld {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  S s;
  uint16_t checksum;
};

static bool s_fromFlash = false;

void settingsProfileDefaults(Settings &s) {
  s.profLowUs     = ESC_UNI_IDLE_US;
  s.profHighUs    = s.escType == ESC_TYPE_BIDI ? PROF_BIDI_HIGH_DEF : PROF_UNI_HIGH_DEF;
  s.profRampUpMs  = 1000;
  s.profDwellHiMs = 3000;
  s.profRampDnMs  = 1000;
  s.profDwellLoMs = 3000;
  s.profCycles    = 10;
  s.testDir       = ESC_DIR_FWD;
}

/* UNI: 1000 <= low < high <= 2000. BIDI: the high end is the forward pulse,
 * 1510-2000 (reverse is its mirror), low is unused. Anything outside is put
 * back to the type's default rather than clamped into something surprising. */
void settingsFixProfileForType(Settings &s) {
  if (s.escType == ESC_TYPE_BIDI) {
    if (s.profHighUs < ESC_BIDI_IDLE_US + 10 || s.profHighUs > ESC_ABS_MAX_US)
      s.profHighUs = PROF_BIDI_HIGH_DEF;
  } else {
    if (s.profLowUs < ESC_ABS_MIN_US || s.profLowUs > ESC_ABS_MAX_US - 10) s.profLowUs = ESC_UNI_IDLE_US;
    if (s.profHighUs <= s.profLowUs || s.profHighUs > ESC_ABS_MAX_US)     s.profHighUs = PROF_UNI_HIGH_DEF;
    if (s.profHighUs <= s.profLowUs) s.profLowUs = ESC_UNI_IDLE_US;
  }
}

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof s);
  s.theme       = THEME_DARK;
  s.filterIndex = 2;          /* 4 samples ≈ 53 ms — visible smoothing,
                               * still well inside a fast pull             */
  s.logMode     = LOGMODE_MANUAL;
  s.logRateIdx  = 0;          /* 76 Hz — every sample                      */
  s.logDurIdx   = 0;          /* until stopped                             */
  s.logThreshA  = 5;          /* above tare drift + nominal-gain error     */
  s.streamOn    = 0;
  s.escType     = ESC_TYPE_UNI;
  s.ctrlStyle   = CTRL_STEP;
  s.releaseMode = RELEASE_HOLD;
  s.testTab     = 0;
  settingsProfileDefaults(s);
}

static uint16_t sumBytes(const uint8_t *p, size_t n) {
  uint16_t c = 0;
  for (size_t i = 0; i < n; i++) c = (uint16_t)(c * 31u + p[i]);
  return c;
}
static uint16_t sumOf(const Settings &s) { return sumBytes((const uint8_t *)&s, sizeof(Settings)); }

static bool inRange(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi; }

/* Guard every field on load. A value that is merely stale is fine; a value
 * that is out of range would drive the ESC or index an array, so it is
 * rejected rather than clamped silently into something plausible. */
static bool plausible(const Settings &s) {
  if (s.theme >= THEME_COUNT) return false;
  if (s.filterIndex >= FILTER_OPTION_COUNT) return false;
  if (s.logMode >= LOGMODE_COUNT) return false;
  if (s.logRateIdx >= LOG_RATE_COUNT) return false;
  if (s.logDurIdx >= LOG_DUR_COUNT) return false;
  if (!inRange(s.logThreshA, LOG_THRESH_MIN_A, LOG_THRESH_MAX_A)) return false;
  if (s.streamOn > 1 || s.escType > 1 || s.ctrlStyle > 1 || s.releaseMode > 1) return false;
  if (s.testDir >= ESC_DIR_COUNT || s.testTab > 2) return false;
  if (!inRange(s.profLowUs, ESC_ABS_MIN_US, ESC_ABS_MAX_US)) return false;
  if (!inRange(s.profHighUs, ESC_ABS_MIN_US, ESC_ABS_MAX_US)) return false;
  if (s.profRampUpMs > PROF_RAMP_MAX_MS || s.profRampDnMs > PROF_RAMP_MAX_MS) return false;
  if (!inRange(s.profDwellHiMs, PROF_DWELL_MIN_MS, PROF_DWELL_MAX_MS)) return false;
  if (!inRange(s.profDwellLoMs, PROF_DWELL_MIN_MS, PROF_DWELL_MAX_MS)) return false;
  if (!inRange(s.profCycles, 1, PROF_CYCLES_MAX)) return false;
  return true;
}

/* Carry an older build's cycle values into the v3 profile. Old dwells could be
 * as short as 200 ms; v3's floor is 500. Old cycles had no ramps, so the
 * migrated profile gets the default 1000 ms ramps — the whole point of v3. */
static void migrateCycle(Settings &m, uint16_t loUs, uint16_t hiUs, uint16_t loMs, uint16_t hiMs) {
  m.profLowUs  = loUs;
  m.profHighUs = hiUs;
  m.profDwellLoMs = loMs < PROF_DWELL_MIN_MS ? PROF_DWELL_MIN_MS : (loMs > PROF_DWELL_MAX_MS ? PROF_DWELL_MAX_MS : loMs);
  m.profDwellHiMs = hiMs < PROF_DWELL_MIN_MS ? PROF_DWELL_MIN_MS : (hiMs > PROF_DWELL_MAX_MS ? PROF_DWELL_MAX_MS : hiMs);
  settingsFixProfileForType(m);
}

template <typename S> static bool readOld(StoredOld<S> &o) {
  EEPROM.get(SETTINGS_ADDR, o);
  return o.size == sizeof(S) && o.checksum == sumBytes((const uint8_t *)&o.s, sizeof(S));
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
  } else if (st.magic == SETTINGS_MAGIC && st.version == 2) {
    StoredOld<SettingsV2> o;
    if (readOld(o)) {
      Settings m; settingsDefaults(m);
      m.theme = o.s.theme;          m.filterIndex = o.s.filterIndex;
      m.logMode = o.s.logMode;      m.logRateIdx = o.s.logRateIdx;
      m.logDurIdx = o.s.logDurIdx;  m.logThreshA = o.s.logThreshA;
      m.streamOn = o.s.streamOn;
      migrateCycle(m, o.s.cycleLoUs, o.s.cycleHiUs, o.s.cycleLoMs, o.s.cycleHiMs);
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
  } else if (st.magic == SETTINGS_MAGIC && st.version == 1) {
    StoredOld<SettingsV1> o;
    if (readOld(o)) {
      Settings m; settingsDefaults(m);
      m.theme = o.s.theme;          m.filterIndex = o.s.filterIndex;
      migrateCycle(m, o.s.cycleLoUs, o.s.cycleHiUs, o.s.cycleLoMs, o.s.cycleHiMs);
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
  }
  themeApply(gSet.theme);
}

bool settingsSave() {
  StoredSettings st;
  memset(&st, 0, sizeof st);
  st.magic    = SETTINGS_MAGIC;
  st.version  = SETTINGS_VERSION;
  st.size     = sizeof(Settings);
  /* No live throttle is stored at all in v3: the manual set point lives only
   * in EscOut and always starts at idle, so a board that resets with a motor
   * attached can never come back under power. */
  st.s = gSet;
  st.checksum = sumOf(st.s);
  EEPROM.put(SETTINGS_ADDR, st);
  bool ok = EEPROM.commit();      /* the few-ms both-core stall lives here */
  if (ok) s_fromFlash = true;
  return ok;
}

bool settingsLoadedFromFlash() { return s_fromFlash; }
