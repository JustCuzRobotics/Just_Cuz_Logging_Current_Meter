#include "Settings.h"
#include "Config.h"
#include "Theme.h"
#include "Clock.h"
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
#define SETTINGS_VERSION 6
#define SETTINGS_ADDR    0

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  Settings s;
  uint16_t checksum;
};

/* Earlier layouts, kept only for migration. Each must stay byte-identical to
 * the struct that build saved. v1 = v3.1c, v2 = v3.2 / v3.2a, v3 = v3.3,
 * v4 = v3.4 (the wall clock, before the pre-roll setting), v5 = the first
 * v3.5 build, before dwells grew to 32 bits. */
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
/* v3 is v4 without the clock fields — everything else is in the same order,
 * so the migration below is a field-for-field copy. */
struct SettingsV3 {
  uint8_t  theme, filterIndex, logMode, logRateIdx, logDurIdx, logThreshA, streamOn;
  uint8_t  escType, ctrlStyle, releaseMode, testDir, testTab;
  uint16_t profLowUs, profHighUs;
  uint16_t profRampUpMs, profDwellHiMs, profRampDnMs, profDwellLoMs, profCycles;
};
static_assert(sizeof(SettingsV3) == 26, "SettingsV3 must match what v3.3 wrote");

/* v4 is v5 without preRollMs, in the same order. */
struct SettingsV4 {
  uint32_t clockEpoch;
  uint8_t  clockEverSet, reserved0;
  uint8_t  theme, filterIndex, logMode, logRateIdx, logDurIdx, logThreshA, streamOn;
  uint8_t  escType, ctrlStyle, releaseMode, testDir, testTab;
  uint16_t profLowUs, profHighUs;
  uint16_t profRampUpMs, profDwellHiMs, profRampDnMs, profDwellLoMs, profCycles;
};
static_assert(sizeof(SettingsV4) == 32, "SettingsV4 must match what v3.4 wrote");

/* v5 is v6 with 16-bit dwells inline among the other uint16s. */
struct SettingsV5 {
  uint32_t clockEpoch;
  uint8_t  clockEverSet, reserved0;
  uint8_t  theme, filterIndex, logMode, logRateIdx, logDurIdx, logThreshA, streamOn;
  uint8_t  escType, ctrlStyle, releaseMode, testDir, testTab;
  uint16_t profLowUs, profHighUs;
  uint16_t profRampUpMs, profDwellHiMs, profRampDnMs, profDwellLoMs, profCycles;
  uint16_t preRollMs, reserved1;
};
static_assert(sizeof(SettingsV5) == 36, "SettingsV5 must match the first v3.5 build");

template <typename S> struct StoredOld {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  S s;
  uint16_t checksum;
};

static bool s_fromFlash = false;
/* The blob exactly as flash last saw it. settingsSaveClock() writes THIS with
 * only the clock refreshed, so a periodic clock save cannot smuggle unsaved
 * screen edits into flash — SAVE stays the only way those get written. */
static Settings s_flashCopy;

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
  s.preRollMs   = PRE_ROLL_DEF_MS;
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
  if (s.clockEverSet > 1) return false;
  if (s.clockEverSet && !clockEpochPlausible(s.clockEpoch)) return false;
  if (s.preRollMs > PRE_ROLL_MAX_MS) return false;
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
static void migrateCycle(Settings &m, uint16_t loUs, uint16_t hiUs, uint32_t loMs, uint32_t hiMs) {
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
  } else if (st.magic == SETTINGS_MAGIC && st.version == 5) {
    StoredOld<SettingsV5> o;
    if (readOld(o)) {
      Settings m; settingsDefaults(m);
      m.clockEpoch = o.s.clockEpoch;    m.clockEverSet = o.s.clockEverSet;
      m.theme = o.s.theme;              m.filterIndex = o.s.filterIndex;
      m.logMode = o.s.logMode;          m.logRateIdx = o.s.logRateIdx;
      m.logDurIdx = o.s.logDurIdx;      m.logThreshA = o.s.logThreshA;
      m.streamOn = o.s.streamOn;        m.escType = o.s.escType;
      m.ctrlStyle = o.s.ctrlStyle;      m.releaseMode = o.s.releaseMode;
      m.testDir = o.s.testDir;          m.testTab = o.s.testTab;
      m.profLowUs = o.s.profLowUs;      m.profHighUs = o.s.profHighUs;
      m.profRampUpMs = o.s.profRampUpMs; m.profDwellHiMs = o.s.profDwellHiMs;
      m.profRampDnMs = o.s.profRampDnMs; m.profDwellLoMs = o.s.profDwellLoMs;
      m.profCycles = o.s.profCycles;    m.preRollMs = o.s.preRollMs;
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
  } else if (st.magic == SETTINGS_MAGIC && st.version == 4) {
    StoredOld<SettingsV4> o;
    if (readOld(o)) {
      Settings m; settingsDefaults(m);
      m.clockEpoch = o.s.clockEpoch;    m.clockEverSet = o.s.clockEverSet;
      m.theme = o.s.theme;              m.filterIndex = o.s.filterIndex;
      m.logMode = o.s.logMode;          m.logRateIdx = o.s.logRateIdx;
      m.logDurIdx = o.s.logDurIdx;      m.logThreshA = o.s.logThreshA;
      m.streamOn = o.s.streamOn;        m.escType = o.s.escType;
      m.ctrlStyle = o.s.ctrlStyle;      m.releaseMode = o.s.releaseMode;
      m.testDir = o.s.testDir;          m.testTab = o.s.testTab;
      m.profLowUs = o.s.profLowUs;      m.profHighUs = o.s.profHighUs;
      m.profRampUpMs = o.s.profRampUpMs; m.profDwellHiMs = o.s.profDwellHiMs;
      m.profRampDnMs = o.s.profRampDnMs; m.profDwellLoMs = o.s.profDwellLoMs;
      m.profCycles = o.s.profCycles;
      /* preRollMs keeps its default: v3.4 had no setting for it. */
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
  } else if (st.magic == SETTINGS_MAGIC && st.version == 3) {
    StoredOld<SettingsV3> o;
    if (readOld(o)) {
      Settings m; settingsDefaults(m);
      m.theme = o.s.theme;              m.filterIndex = o.s.filterIndex;
      m.logMode = o.s.logMode;          m.logRateIdx = o.s.logRateIdx;
      m.logDurIdx = o.s.logDurIdx;      m.logThreshA = o.s.logThreshA;
      m.streamOn = o.s.streamOn;        m.escType = o.s.escType;
      m.ctrlStyle = o.s.ctrlStyle;      m.releaseMode = o.s.releaseMode;
      m.testDir = o.s.testDir;          m.testTab = o.s.testTab;
      m.profLowUs = o.s.profLowUs;      m.profHighUs = o.s.profHighUs;
      m.profRampUpMs = o.s.profRampUpMs; m.profDwellHiMs = o.s.profDwellHiMs;
      m.profRampDnMs = o.s.profRampDnMs; m.profDwellLoMs = o.s.profDwellLoMs;
      m.profCycles = o.s.profCycles;
      /* No clock in a v3 blob: the clock comes up unset, as on a fresh board. */
      if (plausible(m)) { gSet = m; s_fromFlash = true; }
    }
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
  s_flashCopy = gSet;
  clockBegin(gSet.clockEpoch, gSet.clockEverSet != 0);
}

/* Both save paths stamp the blob with the clock as it stands, so whatever
 * reaches flash carries the newest time this board knows. */
static void stampClock(Settings &s) {
  if (clockUsable()) { s.clockEpoch = clockNow(); s.clockEverSet = 1; }
}

static bool writeBlob(const Settings &s) {
  StoredSettings st;
  memset(&st, 0, sizeof st);
  st.magic    = SETTINGS_MAGIC;
  st.version  = SETTINGS_VERSION;
  st.size     = sizeof(Settings);
  st.s        = s;
  st.checksum = sumOf(st.s);
  EEPROM.put(SETTINGS_ADDR, st);
  return EEPROM.commit();         /* the few-ms both-core stall lives here */
}

bool settingsSave() {
  /* No live throttle is stored at all: the manual set point lives only in
   * EscOut and always starts at idle, so a board that resets with a motor
   * attached can never come back under power. */
  stampClock(gSet);
  bool ok = writeBlob(gSet);
  if (ok) { s_fromFlash = true; s_flashCopy = gSet; }
  return ok;
}

bool settingsSaveClock() {
  if (!clockUsable()) return false;
  Settings s = s_flashCopy;
  stampClock(s);
  bool ok = writeBlob(s);
  if (ok) {
    s_flashCopy = s;
    /* Keep the live copy's clock fields in step, so a later SAVE of screen
     * edits does not write back an older timestamp than flash already has. */
    gSet.clockEpoch   = s.clockEpoch;
    gSet.clockEverSet = s.clockEverSet;
  }
  return ok;
}

bool settingsLoadedFromFlash() { return s_fromFlash; }
