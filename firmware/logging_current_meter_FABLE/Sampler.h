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
