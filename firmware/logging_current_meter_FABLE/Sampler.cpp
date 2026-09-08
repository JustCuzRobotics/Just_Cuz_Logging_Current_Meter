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

volatile uint8_t gFilterSamples = 0;      /* set from Settings at boot */
volatile uint32_t gTickOverruns = 0;
volatile uint32_t gTickUsMax    = 0;
volatile bool     gTickMaxResetReq = false;

/* ---- linear weighted moving average, newest sample heaviest --------------
 * Weights are 1..N, so the divisor is N(N+1)/2. Worst case is 20 samples of
 * 15000 centiunits weighted by 20, well inside int32. Kept integer so the
 * sample tick never touches float for something that runs 76 times a second.
 *
 * The ring is written every tick regardless of whether filtering is enabled,
 * so turning the filter on mid-run produces a correct value immediately
 * rather than ramping up from an empty history. */
#define WMA_MAX 20
struct Wma {
  int16_t  buf[WMA_MAX];
  uint8_t  head = 0;
  uint8_t  filled = 0;

  void push(int16_t v) {
    buf[head] = v;
    head = (uint8_t)((head + 1) % WMA_MAX);
    if (filled < WMA_MAX) filled++;
  }
  /* n == 0, or fewer samples than n collected, returns the newest value —
   * never a partially-weighted average, which would read low on the first
   * few ticks after boot. */
  int16_t value(uint8_t n) const {
    uint8_t newest = (uint8_t)((head + WMA_MAX - 1) % WMA_MAX);
    if (n < 2 || filled < n) return buf[newest];
    int32_t acc = 0;
    uint16_t wsum = 0;
    for (uint8_t i = 0; i < n; i++) {
      uint8_t idx = (uint8_t)((head + WMA_MAX - 1 - i) % WMA_MAX);
      uint16_t w  = (uint16_t)(n - i);        /* newest gets the weight n */
      acc  += (int32_t)buf[idx] * w;
      wsum += w;
    }
    return (int16_t)(acc / wsum);
  }
};
static Wma gWmaI, gWmaV;

/* Peak path: the raw sample, passed straight through unless it is physically
 * impossible.
 *
 * A median filter was the obvious choice here and is the wrong one — it
 * rejects EVERY isolated sample, including a genuine 13 ms event, and a brief
 * spike is exactly what this instrument exists to catch. So the only thing
 * screened out is the implausible: the ACS770-150U cannot report more than
 * 150 A and the divider is designed to 60 V, so anything past those is an
 * artefact, not a measurement. The realistic source is the ratiometric divide
 * in iRatio() — it divides by raw5, so a collapse on the 5 V rail inflates the
 * reading — and that failure is wild rather than subtle, which is what makes a
 * range check enough. A modest in-range error from a small rail sag is on the
 * order of an amp, i.e. inside the noise this is not trying to remove. */
#define PEAK_MAX_CENTIAMPS   16000    /* 160 A — sensor tops out at 150 A   */
#define PEAK_MAX_CENTIVOLTS   7000    /* 70 V — design limit is 60 V        */

struct PeakGate {
  int16_t last = 0;
  int16_t limit;
  explicit PeakGate(int16_t lim) : limit(lim) {}
  /* Holds the previous good value when a sample is rejected, so a discarded
   * artefact never registers and never blanks the reading either. */
  int16_t accept(int16_t v) {
    if (v >= 0 && v <= limit) last = v;
    return last;
  }
};
static PeakGate gPeakI(PEAK_MAX_CENTIAMPS), gPeakV(PEAK_MAX_CENTIVOLTS);

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
  if (gTickMaxResetReq) { gTickUsMax = 0; gTickMaxResetReq = false; }
  uint32_t tickT0 = micros();
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

  /* Raw, this tick. Everything below is derived from these. */
  int16_t rawAmps  = (int16_t)lroundf(amps  * 100.0f);
  int16_t rawVolts = (int16_t)lroundf(volts * 100.0f);
  int16_t centidegc = tempOk ? (int16_t)lroundf(tempC * 100.0f) : FIXED_INVALID;

  gWmaI.push(rawAmps);   gWmaV.push(rawVolts);

  uint8_t n = gFilterSamples;                    /* read once — core 0 writes it */
  int16_t centiamps  = gWmaI.value(n);           /* display + graph              */
  int16_t centivolts = gWmaV.value(n);
  int16_t peakAmps   = gPeakI.accept(rawAmps);   /* peaks: raw, range-screened   */
  int16_t peakVolts  = gPeakV.accept(rawVolts);

  /* Displayed power follows the displayed V and I so the numbers agree with
   * each other on screen; peak power is computed from the peak-path values so
   * it is not attenuated by the filter either. */
  int32_t milliwatts     = (int32_t)lroundf((centivolts * 0.01f) * (centiamps * 0.01f) * 1000.0f);
  int32_t peakMilliwatts = (int32_t)lroundf((peakVolts  * 0.01f) * (peakAmps  * 0.01f) * 1000.0f);

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
  /* Raw, deliberately. An integral is inherently smooth, so there is nothing
   * to gain from filtering it and the raw value is the exact one. */
  gState.energyWh  += watts * (float)dtUs * K_WH_PER_US;
  gState.energyMah += amps * 1000.0f * (float)dtUs * K_WH_PER_US;
  gState.runElapsedMs += dtUs / 1000UL;

  /* Peak sag voltage is stored BEFORE peak power, every tick, in that fixed
   * order — so core 0 can never pair a fresh peak power with a stale sag. */
  if (peakAmps > gState.peakCentiamps) gState.peakCentiamps = peakAmps;
  if (peakMilliwatts > gState.peakMilliwatts) {
    gState.peakVAtPeakCentivolts = peakVolts;
    gState.peakMilliwatts = peakMilliwatts;
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

  uint32_t tickUs = micros() - tickT0;
  if (tickUs > gTickUsMax) gTickUsMax = tickUs;
}
