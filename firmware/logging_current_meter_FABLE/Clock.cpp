#include "Clock.h"
#include <Arduino.h>
#include <stdio.h>

/* ------------------------------------------------------------------------
 * Date maths. Howard Hinnant's civil-from-days / days-from-civil, which is
 * exact for any proleptic Gregorian date and needs no tables or loops. The
 * era arithmetic below is his; the only change is fixed-width types.
 * ---------------------------------------------------------------------- */

bool clockLeapYear(int16_t y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

uint8_t clockDaysInMonth(int16_t y, uint8_t m) {
  static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 31;
  if (m == 2 && clockLeapYear(y)) return 29;
  return D[m - 1];
}

static int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);                   /* 0..399   */
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;       /* 0..146096 */
  return era * 146097 + (int32_t)doe - 719468;
}

static void civilFromDays(int32_t z, int32_t &y, uint32_t &m, uint32_t &d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);                /* 0..146096 */
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t yy = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);     /* 0..365    */
  const uint32_t mp = (5 * doy + 2) / 153;                          /* 0..11     */
  d = doy - (153 * mp + 2) / 5 + 1;                                 /* 1..31     */
  m = mp + (mp < 10 ? 3 : -9);                                      /* 1..12     */
  y = yy + (m <= 2);
}

uint32_t clockEpochFromCivil(const CivilTime &c) {
  int32_t days = daysFromCivil(c.year, c.month, c.day);
  if (days < 0) days = 0;
  return (uint32_t)days * 86400UL + (uint32_t)c.hour * 3600UL
       + (uint32_t)c.minute * 60UL + c.second;
}

CivilTime clockCivilFromEpoch(uint32_t epoch) {
  CivilTime c;
  uint32_t days = epoch / 86400UL;
  uint32_t rem  = epoch % 86400UL;
  int32_t y; uint32_t m, d;
  civilFromDays((int32_t)days, y, m, d);
  c.year   = (int16_t)y;
  c.month  = (uint8_t)m;
  c.day    = (uint8_t)d;
  c.hour   = (uint8_t)(rem / 3600UL);
  c.minute = (uint8_t)((rem % 3600UL) / 60UL);
  c.second = (uint8_t)(rem % 60UL);
  return c;
}

void clockFormat(char *out, size_t n, uint32_t epoch, bool withSeconds) {
  CivilTime c = clockCivilFromEpoch(epoch);
  if (withSeconds)
    snprintf(out, n, "%04d-%02u-%02u %02u:%02u:%02u", (int)c.year, (unsigned)c.month,
             (unsigned)c.day, (unsigned)c.hour, (unsigned)c.minute, (unsigned)c.second);
  else
    snprintf(out, n, "%04d-%02u-%02u %02u:%02u", (int)c.year, (unsigned)c.month,
             (unsigned)c.day, (unsigned)c.hour, (unsigned)c.minute);
}

/* ------------------------------------------------------------------------
 * The running clock.
 *
 * base + elapsed, never re-based: re-basing on every read would accumulate
 * the truncation of each division. The millis() difference is unsigned, so
 * the 49.7-day rollover comes out right without a special case.
 * ---------------------------------------------------------------------- */

/* Somewhere sane to start editing from on a board that has never been set.
 * 2026-01-01 00:00:00 local. */
#define CLOCK_FALLBACK_EPOCH 1767225600UL

static uint32_t   s_baseEpoch = CLOCK_FALLBACK_EPOCH;
static uint32_t   s_baseMs    = 0;
static ClockState s_state     = CLK_UNSET;
static bool       s_savePending = false;

bool clockEpochPlausible(uint32_t epoch) {
  return epoch >= CLOCK_EPOCH_MIN && epoch < CLOCK_EPOCH_MAX;
}

void clockBegin(uint32_t storedEpoch, bool everSet) {
  s_baseMs = millis();
  s_savePending = false;
  if (everSet && clockEpochPlausible(storedEpoch)) {
    s_baseEpoch = storedEpoch;
    s_state     = CLK_RESTORED;
  } else {
    s_baseEpoch = CLOCK_FALLBACK_EPOCH;
    s_state     = CLK_UNSET;
  }
}

void clockSet(uint32_t epoch, bool fromSerial) {
  (void)fromSerial;                 /* the source only matters to the caller */
  s_baseEpoch = epoch;
  s_baseMs    = millis();
  s_state     = CLK_SET;
}

/* millis() wraps at 49.7 days. The unsigned subtraction below survives one
 * wrap, but only while the true elapsed time is under 2^32 ms — past that the
 * difference restarts near zero and the clock would jump 49.7 days BACKWARDS
 * and then be written to flash by the periodic save. So the base is carried
 * forward well before the wrap, in whole seconds so nothing is lost. */
static void rebaseIfNeeded() {
  uint32_t el = millis() - s_baseMs;
  if (el < 0x40000000UL) return;              /* ~12.4 days                */
  uint32_t secs = el / 1000UL;
  s_baseEpoch += secs;
  s_baseMs    += secs * 1000UL;
}

uint32_t clockNow() {
  rebaseIfNeeded();
  return s_baseEpoch + (uint32_t)((millis() - s_baseMs) / 1000UL);
}

void clockSaveRequest() { s_savePending = true; }
bool clockSavePending() { return s_savePending; }
void clockSaveDone()    { s_savePending = false; }

ClockState clockState() { return s_state; }
bool       clockUsable() { return s_state != CLK_UNSET; }

const char *clockStateName() {
  switch (s_state) {
    case CLK_SET:      return "SET THIS POWER-UP";
    case CLK_RESTORED: return "RESTORED - MAY BE BEHIND";
    default:           return "NOT SET";
  }
}

const char *clockStateShort() {
  switch (s_state) {
    case CLK_SET:      return "SET";
    case CLK_RESTORED: return "MAY BE BEHIND";
    default:           return "NOT SET";
  }
}

const char *clockStateTag() {
  switch (s_state) {
    case CLK_SET:      return "set";
    case CLK_RESTORED: return "restored";
    default:           return "unset";
  }
}
