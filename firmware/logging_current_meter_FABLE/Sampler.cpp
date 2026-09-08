#include "Sampler.h"
#include <math.h>

volatile SampleState gState;
volatile bool gCmdTare = false;
volatile bool gCmdResetEnergyTimer = false;
volatile bool gCmdResetPeaks = false;

int16_t gRingI[GRAPH_COLS], gRingV[GRAPH_COLS], gRingT[GRAPH_COLS];
volatile uint16_t gRingHead = 0;

/* Runtime-only zero offset. Never touches the baked I_QUIESCENT_CAL, and is
 * lost on reboot by design — a tare reflects the setup in front of you. */
static float gTareOffsetA = 0.0f;

/* 20 us spacing between reads: the RP2040 ADC has differential-nonlinearity
 * artefacts near code boundaries that averaging back-to-back samples does not
 * clear. 64 x 4 channels is about 5 ms of the 13.158 ms budget. */
static uint16_t adcAvg(uint8_t pin) {
  uint32_t acc = 0;
  for (uint16_t i = 0; i < ADC_AVG; i++) { acc += analogRead(pin); delayMicroseconds(20); }
  return (uint16_t)(acc / ADC_AVG);
}

static float packVolts(uint16_t raw) { return V_GAIN_CAL * (float)raw + V_OFFSET_CAL; }

/* Both VCC and VREF cancel out of this ratio (DESIGN.md 4.2), which is what
 * makes the current reading independent of USB rail noise. */
static float iRatio(uint16_t rawI, uint16_t raw5) {
  if (raw5 < 100) return 0.0f;
  return (float)rawI / (float)raw5 * G2_OVER_G1;
}

/* NTC through RV1, B-parameter form. x > 0.97 is an open circuit, x < 0.02 a
 * short: report the fault rather than a plausible-looking wrong temperature. */
static bool tempFromRawC(uint16_t rawT, float *outC) {
  float x = rawT / ADC_MAX;
  if (x > 0.97f || x < 0.02f) return false;
  float rntc = RV1_OHMS * x / (1.0f - x);
  *outC = 1.0f / (1.0f / 298.15f + logf(rntc / NTC_R25) / NTC_B) - 273.15f;
  return true;
}

void samplerTick(void (*interleave)()) {
  uint16_t rawI = adcAvg(PIN_I_SENSE);   if (interleave) interleave();
  uint16_t raw5 = adcAvg(PIN_V5_SENSE);  if (interleave) interleave();
  uint16_t rawV = adcAvg(PIN_V_PACK);    if (interleave) interleave();
  uint16_t rawT = adcAvg(PIN_T_SENSE);   if (interleave) interleave();

  float ratio   = iRatio(rawI, raw5);
  float ampsRaw = (ratio - I_QUIESCENT_CAL) / I_SENS_CAL;

  /* Tare zeroes the reading only, gated against the nominal ~0.1 quiescent
   * ratio so a tare taken under load can't bake in a huge offset. */
  if (gCmdTare) {
    if (ratio > 0.05f && ratio < 0.15f) gTareOffsetA = -ampsRaw;
    gCmdTare = false;
  }
  float amps = ampsRaw + gTareOffsetA;
  if (amps < 0.0f) amps = 0.0f;

  float volts = packVolts(rawV);
  float tempC; bool tempOk = tempFromRawC(rawT, &tempC);
  float watts = volts * amps;

  int16_t centiamps  = (int16_t)lroundf(amps  * 100.0f);
  int16_t centivolts = (int16_t)lroundf(volts * 100.0f);
  int16_t centidegc  = tempOk ? (int16_t)lroundf(tempC * 100.0f) : FIXED_INVALID;
  int32_t milliwatts = (int32_t)lroundf(watts * 1000.0f);

  gState.centiamps  = centiamps;
  gState.centivolts = centivolts;
  gState.centidegc  = centidegc;
  gState.milliwatts = milliwatts;
  gState.tempFault  = !tempOk;

  /* Energy integrates against the MEASURED interval, not the nominal tick, so
   * drift in loop timing doesn't bias the accumulated total. */
  static uint32_t lastTickUs = 0;
  uint32_t nowUs = micros();
  uint32_t dtUs = lastTickUs ? (nowUs - lastTickUs) : (uint32_t)TICK_US;
  lastTickUs = nowUs;
  const float K_WH_PER_US = 1.0f / 3600000000.0f;
  gState.energyWh  += watts * (float)dtUs * K_WH_PER_US;
  gState.energyMah += amps * 1000.0f * (float)dtUs * K_WH_PER_US;
  gState.runElapsedMs += dtUs / 1000UL;

  /* Peak sag voltage is stored BEFORE peak power, every tick, in that fixed
   * order — so core 0 can never pair a fresh peak power with a stale sag. */
  if (centiamps > gState.peakCentiamps) gState.peakCentiamps = centiamps;
  if (milliwatts > gState.peakMilliwatts) {
    gState.peakVAtPeakCentivolts = centivolts;
    gState.peakMilliwatts = milliwatts;
  }
  if (tempOk && centidegc > gState.peakCentidegc) gState.peakCentidegc = centidegc;

  if (!gState.mark2Captured && gState.runElapsedMs >= 120000UL) {
    gState.mark2Wh = gState.energyWh; gState.mark2Mah = gState.energyMah;
    gState.mark2Captured = true;
  }
  if (!gState.mark3Captured && gState.runElapsedMs >= 180000UL) {
    gState.mark3Wh = gState.energyWh; gState.mark3Mah = gState.energyMah;
    gState.mark3Captured = true;
  }

  /* Peak reset takes temperature to the CURRENT reading, not zero — 0 C is
   * not a sensible floor for a peak that starts above it. */
  if (gCmdResetPeaks) {
    gState.peakCentiamps = 0;
    gState.peakMilliwatts = 0;
    gState.peakVAtPeakCentivolts = 0;
    gState.peakCentidegc = tempOk ? centidegc : FIXED_INVALID;
    gCmdResetPeaks = false;
  }

  /* Energy reset and timer reset are one combined action, deliberately. */
  if (gCmdResetEnergyTimer) {
    gState.energyWh = 0.0f; gState.energyMah = 0.0f;
    gState.runElapsedMs = 0;
    gState.mark2Captured = gState.mark3Captured = false;
    gCmdResetEnergyTimer = false;
  }

  /* On a thermistor fault carry the last good sample forward rather than
   * plotting the fault sentinel as if it were a reading. */
  int16_t tForRing = tempOk ? centidegc : gRingT[gRingHead];
  uint16_t h = (uint16_t)((gRingHead + 1) % GRAPH_COLS);
  gRingI[h] = centiamps;
  gRingV[h] = centivolts;
  gRingT[h] = tForRing;
  gRingHead = h;
}
