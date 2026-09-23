/* ==========================================================================
 * ScreenClock.cpp — SET CLOCK, reached from the Settings clock row.
 *
 *   [YEAR][MONTH][DAY][HOUR][MIN]      tap a field to select it
 *   [-big][-small]  VALUE  [+small][+big]       (hold to repeat)
 *              2026-09-23 14:05                 what APPLY will set
 *   live clock + what the clock currently is
 *   [APPLY]
 *
 * Editing works on a copy. Nothing reaches the clock until APPLY, which sets
 * the time with seconds = 0 — so the way to be accurate by hand is to dial in
 * the next minute and press APPLY as it arrives. For real accuracy, the
 * laptop's `c <epoch>` sets it to the second (see the .ino serial keys).
 *
 * APPLY also writes the clock to flash so it survives the next power cycle,
 * which is the one thing it cannot do during a capture: an EEPROM commit
 * stalls both cores. During a log the clock is still set in RAM and the toast
 * says the save is deferred — logStop() writes it.
 * ========================================================================*/
#include "Screens.h"
#include "Widgets.h"
#include "Settings.h"
#include "Clock.h"
#include "Logger.h"

static CivilTime sEdit;             /* the working copy                      */
static uint8_t   sSel = CFLD_HOUR;  /* the field most likely to need a nudge */
/* paintClockOnce() runs again whenever a toast expires, so the working copy
 * is seeded on ENTRY only — otherwise a toast clearing 1.6 s after APPLY
 * would silently wipe whatever had been dialled in since. */
static bool      sEditValid = false;

/* ---- field maths ------------------------------------------------------ */

static void fieldRange(uint8_t f, int16_t &lo, int16_t &hi) {
  switch (f) {
    case CFLD_YEAR:  lo = CLOCK_YEAR_MIN; hi = CLOCK_YEAR_MAX; break;
    case CFLD_MONTH: lo = 1;  hi = 12; break;
    case CFLD_DAY:   lo = 1;  hi = clockDaysInMonth(sEdit.year, sEdit.month); break;
    case CFLD_HOUR:  lo = 0;  hi = 23; break;
    default:         lo = 0;  hi = 59; break;
  }
}

/* Small and big step for each field. Big is the lime button. */
static void fieldSteps(uint8_t f, int16_t &small, int16_t &big) {
  small = 1;
  big   = (f == CFLD_MONTH) ? 3 : 10;    /* a quarter, or a decade/hour-ish */
}

static int16_t fieldValue(uint8_t f) {
  switch (f) {
    case CFLD_YEAR:  return sEdit.year;
    case CFLD_MONTH: return sEdit.month;
    case CFLD_DAY:   return sEdit.day;
    case CFLD_HOUR:  return sEdit.hour;
    default:         return sEdit.minute;
  }
}

/* Day is clamped rather than wrapped after a month or year change: 31 March
 * stepped back to February should become the 28th (or 29th), not January. */
static void clampDay() {
  uint8_t dim = clockDaysInMonth(sEdit.year, sEdit.month);
  if (sEdit.day > dim) sEdit.day = dim;
}

static void fieldSet(uint8_t f, int16_t v) {
  switch (f) {
    case CFLD_YEAR:  sEdit.year   = v; clampDay(); break;
    case CFLD_MONTH: sEdit.month  = (uint8_t)v; clampDay(); break;
    case CFLD_DAY:   sEdit.day    = (uint8_t)v; break;
    case CFLD_HOUR:  sEdit.hour   = (uint8_t)v; break;
    default:         sEdit.minute = (uint8_t)v; break;
  }
}

/* Every field wraps, so holding + on MINUTE rolls 59 to 0 rather than
 * sticking. Nothing carries into the next field: the operator is setting a
 * date, not counting up to one. */
static void stepField(int16_t delta) {
  int16_t lo, hi;
  fieldRange(sSel, lo, hi);
  int16_t span = (int16_t)(hi - lo + 1);
  int32_t v = (int32_t)fieldValue(sSel) + delta - lo;
  v %= span;
  if (v < 0) v += span;
  fieldSet(sSel, (int16_t)(v + lo));
}

/* ---- drawing ---------------------------------------------------------- */

static const char *const FIELD_LABEL[CFLD_N] = { "YEAR", "MONTH", "DAY", "HOUR", "MIN" };

static void fieldText(uint8_t f, char *out, size_t n) {
  if (f == CFLD_YEAR) snprintf(out, n, "%d", (int)sEdit.year);
  else                snprintf(out, n, "%02d", (int)fieldValue(f));
}

static void drawFieldTile(uint8_t f) {
  char v[8];
  fieldText(f, v, sizeof v);
  drawTile(CLK_T[CLK_TILE0 + f].vis, FIELD_LABEL[f], v, f == sSel, true);
}

static void editorLabel(uint8_t which, char *out, size_t n) {
  int16_t small, big;
  fieldSteps(sSel, small, big);
  int16_t s = (which == 0 || which == 3) ? big : small;
  snprintf(out, n, "%c%d", (which < 2) ? '-' : '+', (int)s);
}

static void drawEditorBtn(uint8_t which, bool pressed) {
  char lab[8];
  editorLabel(which, lab, sizeof lab);
  drawDeltaBtn(CLK_T[CLK_BIG_M + which].vis, pressed, lab, which == 0 || which == 3);
}

static void drawValueBox(bool forceClear) {
  static char cache[16];
  if (forceClear) cache[0] = 0;
  char v[8];
  fieldText(sSel, v, sizeof v);
  drawStepperBox(CLK_VALUE_BOX, v, COL_VOLT, cache);
}

/* The working value, big. Repainted only when it changes, like every other
 * cached readout on this instrument. */
static void drawBigValue(bool forceClear) {
  static char cache[24];
  char buf[24];
  CivilTime c = sEdit;
  c.second = 0;
  clockFormat(buf, sizeof buf, clockEpochFromCivil(c), false);
  if (!forceClear && strcmp(cache, buf) == 0) return;
  strncpy(cache, buf, sizeof cache - 1);
  cache[sizeof cache - 1] = 0;
  tft.fillRect(0, CLK_BIG_Y, tft.width(), RUSSO22.height + 4, COL_BG);
  tRussoCentered(RUSSO22, tft.width() / 2, CLK_BIG_Y, buf, COL_TEXT_HI, COL_BG);
}

void paintClockOnce() {
  tft.fillScreen(COL_BG);
  drawBackBtn(CLK_T[CLK_BACK].vis, false);
  tRussoCentered(RUSSO16, tft.width() / 2, 6, "SET CLOCK", COL_TEXT_HI, COL_BG);

  /* Start from the clock as it stands, so a correction is a nudge, not a
   * re-entry of the whole date. */
  if (!sEditValid) {
    sEdit = clockCivilFromEpoch(clockNow());
    sEdit.second = 0;
    if (sEdit.year < CLOCK_YEAR_MIN) sEdit.year = CLOCK_YEAR_MIN;
    if (sEdit.year > CLOCK_YEAR_MAX) sEdit.year = CLOCK_YEAR_MAX;
    sEditValid = true;
  }

  for (uint8_t f = 0; f < CFLD_N; f++) drawFieldTile(f);
  for (uint8_t w = 0; w < 4; w++) drawEditorBtn(w, false);
  drawLabelBtn(CLK_T[CLK_APPLY].vis, false, "APPLY", COL_VOLT, true);
  updateClockTick(true);
}

void updateClockTick(bool forceClear) {
  static char cLive[CLK_STAT_CHARS + 1], cHint[CLK_STAT_CHARS + 1];
  if (forceClear) { cLive[0] = 0; cHint[0] = 0; }
  drawValueBox(forceClear);
  drawBigValue(forceClear);

  char now[24], buf[CLK_STAT_CHARS + 1];
  clockFormat(now, sizeof now, clockNow());
  snprintf(buf, sizeof buf, "CLOCK NOW  %s  %s", now, clockStateName());
  field5(CLK_STAT_X, CLK_STAT_Y1, CLK_STAT_CHARS, 1, COL_TEXT_HI, COL_BG, buf, cLive);

  snprintf(buf, sizeof buf, "APPLY SETS SECONDS TO 00. FROM A LAPTOP: c <EPOCH> SETS IT EXACTLY");
  field5(CLK_STAT_X, CLK_STAT_Y2, CLK_STAT_CHARS, 1, COL_TEXT, COL_BG, buf, cHint);
}

/* ---- interaction ------------------------------------------------------ */

int8_t clockHit(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < CLK_BTN_N; i++)
    if (CLK_T[i].hit.contains(x, y)) return (int8_t)i;
  return -1;
}

void clockSetPressed(int8_t id, bool pressed) {
  if (id == CLK_BACK) { drawBackBtn(CLK_T[CLK_BACK].vis, pressed); return; }
  if (id >= CLK_BIG_M && id <= CLK_BIG_P) { drawEditorBtn((uint8_t)(id - CLK_BIG_M), pressed); return; }
  if (id == CLK_APPLY) drawLabelBtn(CLK_T[CLK_APPLY].vis, pressed, "APPLY", COL_VOLT, true);
}

bool clockRepeatable(int8_t id) { return id >= CLK_BIG_M && id <= CLK_BIG_P; }

void clockDispatch(int8_t id) {
  if (id == CLK_BACK) { sEditValid = false; goTo(SCR_SETTINGS); return; }

  if (id >= CLK_TILE0 && id < (int)CLK_TILE0 + (int)CFLD_N) {
    uint8_t f = (uint8_t)(id - CLK_TILE0);
    if (f == sSel) return;
    uint8_t old = sSel;
    sSel = f;
    drawFieldTile(old);
    drawFieldTile(f);
    for (uint8_t w = 0; w < 4; w++) drawEditorBtn(w, false);   /* steps change */
    updateClockTick(true);
    return;
  }

  if (id >= CLK_BIG_M && id <= CLK_BIG_P) {
    int16_t small, big;
    fieldSteps(sSel, small, big);
    uint8_t which = (uint8_t)(id - CLK_BIG_M);
    int16_t step = (which == 0 || which == 3) ? big : small;
    stepField((which < 2) ? (int16_t)-step : step);
    drawFieldTile(sSel);
    if (sSel == CFLD_YEAR || sSel == CFLD_MONTH) drawFieldTile(CFLD_DAY);
    updateClockTick();
    return;
  }

  if (id == CLK_APPLY) {
    CivilTime c = sEdit;
    c.second = 0;
    clockSet(clockEpochFromCivil(c), false);
    /* Writing flash stalls both cores, so it waits for a quiet bench. The
     * clock itself is set either way — only the copy that survives a power
     * cycle is deferred. */
    if (clockSaveSoon())          showToast("Clock set and saved");
    else if (clockSaveBlocked())  showToast("Clock set - saved when idle");
    else                          showToast("Clock set - flash save FAILED");
    updateClockTick(true);
  }
}
