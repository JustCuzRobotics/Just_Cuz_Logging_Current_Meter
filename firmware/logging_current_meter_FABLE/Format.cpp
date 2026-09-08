#include "Format.h"

static void fmt3SigFig(int32_t whole, uint16_t frac100, char *buf, size_t n) {
  if (whole < 10)       snprintf(buf, n, "%ld.%02u", (long)whole, frac100);
  else if (whole < 100) snprintf(buf, n, "%ld.%u",   (long)whole, frac100 / 10);
  else                  snprintf(buf, n, "%ld",      (long)whole);
}

void fmt3SigCenti(int16_t centi, char *buf, size_t n) {
  if (centi == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  /* With no pack connected V_OFFSET_CAL reads about -0.14 V. Clamp, or the
   * unsigned modulo below prints garbage ("0.65522"). */
  if (centi < 0) centi = 0;
  fmt3SigFig(centi / 100, (uint16_t)(centi % 100), buf, n);
}

void fmt3SigWatts(int32_t milliwatts, char *buf, size_t n) {
  if (milliwatts < 0) milliwatts = 0;
  fmt3SigFig(milliwatts / 1000, (uint16_t)((milliwatts % 1000) / 10), buf, n);
}

void fmtDegC(int16_t centidegc, char *buf, size_t n) {
  if (centidegc == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  snprintf(buf, n, "%dC", centidegc / 100);
}

void fmtTimer(uint32_t ms, char *buf, size_t n) {
  uint32_t totalSec = ms / 1000, m = totalSec / 60, s = totalSec % 60, msPart = ms % 1000;
  snprintf(buf, n, "%02lu:%02lu.%03lu", (unsigned long)m, (unsigned long)s, (unsigned long)msPart);
}

void fmtMark(bool captured, float wh, float mah, char *buf, size_t n) {
  if (!captured) snprintf(buf, n, "---");
  else           snprintf(buf, n, "%.2fWh %.0fmA", wh, mah);
}

void fmtWhole(int16_t centi, char *buf, size_t n) {
  if (centi == FIXED_INVALID) { snprintf(buf, n, "ERR"); return; }
  snprintf(buf, n, "%d", centi / 100);
}

/* One tenths digit, for the current axis only: at a small autoscale ceiling
 * (a 2 A ceiling is 0.4 A per gridline) whole numbers collapse five gridlines
 * into "0 0 0 1 1 2". V/T spans are always coarse enough not to need this. */
void fmtAxisAmps(int16_t centi, char *buf, size_t n) {
  snprintf(buf, n, "%d.%d", centi / 100, (centi % 100) / 10);
}
