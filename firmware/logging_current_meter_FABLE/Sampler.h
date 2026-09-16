/* ==========================================================================
 * Sampler.h — core-1 instrumentation: the ADC, the calibration maths, and the
 * graph history ring.
 *
 * OWNERSHIP RULE (this is what makes it safe without a mutex):
 * every field of SampleState is written by core 1 only and read by core 0
 * only. A 16- or 32-bit aligned store is a single atomic bus transaction on
 * the RP2040, so core 0 can never see a torn value — at worst it reads last
 * tick's number, which is invisible at UI refresh rates. The three command
 * flags are the one place core 0 writes: core 0 only ever sets a flag TRUE,
 * core 1 only ever clears it. One writer per direction, so no race either way.
 *
 * Core 0 must never call anything in here that touches the ADC.
 * ========================================================================*/
#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Format.h"

#define GRAPH_COLS   380     /* = the plot's pixel width, one sample each */

struct SampleState {
  int16_t  centiamps, centivolts, centidegc;
  int32_t  milliwatts;
  float    energyWh, energyMah;      /* the deliberate float exception     */
  bool     mark2Captured, mark3Captured;
  float    mark2Wh, mark2Mah, mark3Wh, mark3Mah;
  uint32_t runElapsedMs;
  int16_t  peakCentiamps, peakVAtPeakCentivolts, peakCentidegc;
  int32_t  peakMilliwatts;
  bool     tempFault;
};

extern volatile SampleState gState;
extern volatile bool gCmdTare, gCmdResetEnergyTimer, gCmdResetPeaks;

/* Graph history. gRingHead indexes the NEWEST sample and is advanced only
 * after its data is stored, so core 0 can never read a half-written slot. */
extern int16_t gRingI[GRAPH_COLS], gRingV[GRAPH_COLS], gRingT[GRAPH_COLS];
extern volatile uint16_t gRingHead;

/* Runs one 13.158 ms acquisition. Call from core 1 only. `interleave`, if
 * given, is invoked between channels — the meter passes touch servicing so a
 * ~5 ms oversampling burst can never delay a touch sample by more than one
 * channel (~1.3 ms). */
void samplerTick(void (*interleave)() = nullptr);

/* ---- reading filter -------------------------------------------------------
 * Three different sources feed three different consumers, because they want
 * different things:
 *
 *   displayed numbers, graph trace   linear WMA over N samples (readability)
 *   peaks                            raw, median-of-3        (fidelity)
 *   energy / mAh                     raw                     (exactness)
 *
 * The WMA weights the newest sample heaviest (weights 1..N), so it settles a
 * noisy reading with far less lag than a flat average of the same width.
 *
 * Peaks deliberately skip it. Current noise here is about 0.2 A against peaks
 * of 10-100 A, so there is nothing for a filter to protect a peak from, and
 * smoothing would only cost real spike height. Median-of-3 is there instead:
 * it removes an isolated bad sample outright while passing anything lasting
 * two ticks or more untouched. The sample worth removing is not ADC noise but
 * the ratiometric divide — iRatio() divides by raw5, so a momentary sag on the
 * 5 V rail inflates one current reading. Any real event (stall, prop strike,
 * spin-up) lasts far longer than 13 ms, so nothing genuine is lost.
 *
 * Core 0 writes the window, core 1 reads it: one writer per direction, the
 * same rule the command flags follow. */
extern volatile uint8_t gFilterSamples;   /* 0 = off, else 2..20 */

/* Core-1 health, for the touch-responsiveness telemetry. Written by core 1,
 * read by core 0. tickOverruns counts the resync branch in loop1() — the
 * sampler fell more than four ticks behind; tickUsMax is the longest single
 * samplerTick() including its interleaved touch services. If touch service
 * rate ever drops, these say whether core 1 was the reason. */
extern volatile uint32_t gTickOverruns;
extern volatile uint32_t gTickUsMax;
extern volatile bool     gTickMaxResetReq;   /* core 0 raises, core 1 clears */

/* ---- log record ring (v3.2) ----------------------------------------------
 * One record per sample tick, pushed by core 1 at the END of samplerTick()
 * (after any reset command has been applied), drained by core 0 into the SD
 * log and the USB stream.
 *
 * Single producer, single consumer, no lock: core 1 is the only writer of
 * gLogHead and core 0 the only writer of gLogTail. Core 1 advances the head
 * only after the slot is fully written, so core 0 can never read a half-
 * written record. If core 0 falls a whole ring behind (~6.7 s — an SD card
 * stalling far beyond anything normal) core 1 drops the NEW record rather
 * than overwrite one core 0 may be reading, and counts it in gLogRingDrops.
 *
 * Records carry raw AND filtered V/I: raw is what the instrument measured,
 * filtered is what the screen showed. Power is raw V x raw I, the same
 * product energy integrates. energyEpoch increments every time core 1 applies
 * an energy reset, so a consumer can tell a reset apart from a counter that
 * merely went backwards. */
struct LogRec {
  uint32_t seq;            /* tick number since boot                        */
  uint32_t tMs;            /* core-1 millis() at the end of the tick        */
  int16_t  iRaw, iFilt;    /* centiamps                                     */
  int16_t  vRaw, vFilt;    /* centivolts                                    */
  int16_t  tC;             /* centidegC, FIXED_INVALID on thermistor fault  */
  uint16_t escUs;          /* pulse on the ESC pin this tick, 0 = off       */
  int32_t  mWRaw;          /* milliwatts, raw V x raw I                     */
  float    mah, wh;        /* cumulative since the last energy reset        */
  uint16_t energyEpoch;
};

#define LOG_RING_N  512    /* x 13.158 ms = 6.7 s of slack for SD stalls    */
extern LogRec gLogRing[LOG_RING_N];
extern volatile uint16_t gLogHead;      /* core 1 writes: next free slot     */
extern volatile uint16_t gLogTail;      /* core 0 writes: next to read       */
extern volatile uint32_t gLogRingDrops;
extern volatile uint16_t gEnergyEpoch;
