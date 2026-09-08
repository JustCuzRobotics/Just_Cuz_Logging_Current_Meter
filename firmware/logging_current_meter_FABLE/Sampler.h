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
