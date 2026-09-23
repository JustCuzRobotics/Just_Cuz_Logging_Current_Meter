/* ==========================================================================
 * Clock.h — wall clock for log metadata.
 *
 * There is no RTC chip and no backup cell on this board, so the clock is
 * software: an epoch base captured when it was last set, plus millis() since.
 * That is enough to stamp a log with the day and time it was recorded, which
 * is all it is for. It is NOT a time source for anything that matters — the
 * sample timebase stays the 13.158 ms tick, untouched.
 *
 * Power cycles: the epoch is written to the settings blob at log boundaries,
 * on an explicit Save, whenever the clock is set, and every 5 minutes while
 * idle. On boot the clock resumes from that value, so it comes back reading
 * the moment of the last save — right date, time behind by however long the
 * meter was off. clockState() reports that as RESTORED so the log header, the
 * Settings screen and the analyzer can all say so rather than implying a
 * precision the number does not have.
 *
 * All times are LOCAL. No timezone, no DST: whatever wall time is set is what
 * gets stored, which is what a bench log wants.
 *
 * Drift between syncs is the RP2040 crystal's, +-30 ppm worst case, about
 * 2.6 s/day. A serial sync at the start of a session (capture_stream.py sends
 * one automatically) keeps a log's stamps accurate to the second.
 *
 * Core 0 only — it reads millis() and is called from the UI, serial and
 * logger paths. Core 1 never touches it.
 * ========================================================================*/
#pragma once
#include <stdint.h>
#include <stddef.h>

enum ClockState : uint8_t {
  CLK_UNSET = 0,     /* never set on this board: no trustworthy date at all  */
  CLK_RESTORED,      /* from flash: date good, time behind by the off-time   */
  CLK_SET,           /* set this power-up, from the screen or serial         */
};

/* ---- pure date maths (no Arduino, host-testable) ---------------------- */

struct CivilTime {
  int16_t year;      /* 1970..2100                                          */
  uint8_t month;     /* 1..12                                               */
  uint8_t day;       /* 1..31                                               */
  uint8_t hour;      /* 0..23                                               */
  uint8_t minute;    /* 0..59                                               */
  uint8_t second;    /* 0..59                                               */
};

#define CLOCK_YEAR_MIN 2020
#define CLOCK_YEAR_MAX 2099
/* The same window as epoch seconds: 2020-01-01 to 2100-01-01. Anything
 * outside it is a typo, a milliseconds-instead-of-seconds mistake or a
 * corrupt blob, and is refused rather than stamped into a log. */
#define CLOCK_EPOCH_MIN 1577836800UL
#define CLOCK_EPOCH_MAX 4102444800UL
bool clockEpochPlausible(uint32_t epoch);

bool     clockLeapYear(int16_t year);
uint8_t  clockDaysInMonth(int16_t year, uint8_t month);
uint32_t clockEpochFromCivil(const CivilTime &c);   /* seconds since 1970    */
CivilTime clockCivilFromEpoch(uint32_t epoch);
/* "2026-09-23 14:05:30" — needs 20 bytes. Short form is "2026-09-23 14:05". */
void     clockFormat(char *out, size_t n, uint32_t epoch, bool withSeconds = true);

/* ---- the running clock ------------------------------------------------ */

/* Called once from setup() with the epoch read out of the settings blob.
 * everSet = false means the blob has never held a real time (fresh board or
 * a migrated older blob), and the clock comes up UNSET. */
void       clockBegin(uint32_t storedEpoch, bool everSet);
void       clockSet(uint32_t epoch, bool fromSerial);
uint32_t   clockNow();                /* local epoch seconds                 */
ClockState clockState();
bool       clockUsable();             /* state != CLK_UNSET                   */
/* Long form for the clock screen, short form for the one-line Settings row
 * (37 characters of 5x7 there, so the long one would be cut off). */
const char *clockStateName();    /* "SET THIS POWER-UP" / "RESTORED - MAY BE BEHIND" / "NOT SET" */
const char *clockStateShort();   /* "SET" / "MAY BE BEHIND" / "NOT SET"    */
const char *clockStateTag();          /* "set" / "restored" / "unset"         */

/* ---- getting the clock into flash -------------------------------------
 * Writing it means an EEPROM commit, which stalls BOTH cores for a few ms.
 * That is unacceptable while a log is recording or while the ESC output is
 * live (the touch controller is serviced on core 1, so a stall is a few ms
 * in which the STOP button does not respond). So nothing here writes flash
 * on its own: a caller asks, and the main loop writes at the next safe
 * moment. clockSaveBlocked() and clockSaveSoon() live in the sketch, which
 * is the only place that knows what the logger and the ESC are doing. */
bool clockSaveBlocked();     /* a log is recording, or the ESC is live      */
bool clockSaveSoon();        /* true if it went to flash right now          */
void clockSaveRequest();     /* remember that flash is behind the clock     */
bool clockSavePending();
void clockSaveDone();
