/* ==========================================================================
 * Format.h — fixed-point value formatters.
 *
 * Physical quantities are integers throughout: centiamps/centivolts/centidegc
 * are value x100 in int16_t, milliwatts are W x1000 in int32_t (centiwatts
 * would overflow int16_t at 150 A x 65 V). FIXED_INVALID marks "no valid
 * reading" — currently only a faulted thermistor. Nothing here touches float
 * just to print a number.
 * ========================================================================*/
#pragma once
#include <Arduino.h>

#define FIXED_INVALID  ((int16_t)-32768)

/* Adaptive 3-significant-figure: below 10 two decimals, 10-99 one, >=100
 * none — i.e. 0.27 -> 2.20 -> 44.0 -> 100. */
void fmt3SigCenti(int16_t centi, char *buf, size_t n);
void fmt3SigWatts(int32_t milliwatts, char *buf, size_t n);
void fmtDegC(int16_t centidegc, char *buf, size_t n);      /* whole degrees + C */
void fmtTimer(uint32_t ms, char *buf, size_t n);            /* mm:ss.mmm        */
void fmtMark(bool captured, float wh, float mah, char *buf, size_t n);
void fmtWhole(int16_t centi, char *buf, size_t n);          /* V/T axis labels  */
void fmtAxisAmps(int16_t centi, char *buf, size_t n);       /* current axis     */
