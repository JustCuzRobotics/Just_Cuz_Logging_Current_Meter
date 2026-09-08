/* ==========================================================================
 * EscOut.cpp — see EscOut.h for the design rationale.
 * ========================================================================*/
#include "EscOut.h"
#include "Config.h"
#include "Settings.h"

static bool     sArmed    = false;
static bool     sCycling  = false;
static bool     sAtHigh   = false;
static uint16_t sPulseUs  = ESC_PULSE_MIN_US;
static uint16_t sPeriodUs = 20000;
static uint32_t sPhaseEndMs = 0;

/* ---- helpers ---------------------------------------------------------- */

static uint16_t clampPulse(uint16_t us) {
  if (us < ESC_PULSE_MIN_US) return ESC_PULSE_MIN_US;
  if (us > ESC_PULSE_MAX_US) return ESC_PULSE_MAX_US;
  return us;
}

static uint16_t clampPeriod(uint16_t us) {
  if (us < ESC_PERIOD_MIN_US) return ESC_PERIOD_MIN_US;
  if (us > ESC_PERIOD_MAX_US) return ESC_PERIOD_MAX_US;
  return us;
}

/* Push the current pulse/period to the PWM slice. Only ever called while
 * armed — a disarmed pin is a plain LOW output, not a zero-width frame. */
static void applyPwm() {
  analogWriteFreq((uint32_t)(1000000UL / sPeriodUs));
  analogWriteRange(sPeriodUs);
  analogWrite(PIN_ESC_SIG, sPulseUs);
}

/* ---- lifecycle -------------------------------------------------------- */

void escBegin() {
  sArmed   = false;
  sCycling = false;
  sAtHigh  = false;
  sPulseUs  = clampPulse(gSet.escPulseUs);
  sPeriodUs = clampPeriod(gSet.escPeriodUs);
  pinMode(PIN_ESC_SIG, OUTPUT);
  digitalWrite(PIN_ESC_SIG, LOW);
}

/* ---- manual ----------------------------------------------------------- */

void escArm(bool on) {
  if (on == sArmed) { if (on) applyPwm(); return; }
  sArmed = on;
  if (on) {
    applyPwm();
  } else {
    sCycling = false;
    /* Back to a plain digital LOW. analogWrite() has taken the pin, so the
     * pinMode call is what hands GPIO control back to the SIO block. */
    pinMode(PIN_ESC_SIG, OUTPUT);
    digitalWrite(PIN_ESC_SIG, LOW);
  }
}

bool escArmed() { return sArmed; }

void escSetPulse(uint16_t us) {
  sPulseUs = clampPulse(us);
  if (sArmed) analogWrite(PIN_ESC_SIG, sPulseUs);
}

uint16_t escPulse() { return sPulseUs; }

void escSetPeriod(uint16_t us) {
  sPeriodUs = clampPeriod(us);
  if (sArmed) applyPwm();      /* frequency change needs the full re-apply */
}

uint16_t escPeriod() { return sPeriodUs; }

/* ---- auto-cycle ------------------------------------------------------- */

/* A dwell of 0 ms would spin the state machine every tick, so both dwells are
 * floored at one tick's worth of time. */
static uint32_t dwellLo() { return gSet.cycleLoMs ? gSet.cycleLoMs : 1; }
static uint32_t dwellHi() { return gSet.cycleHiMs ? gSet.cycleHiMs : 1; }

void escCycleStart() {
  sAtHigh  = false;
  sPulseUs = clampPulse(gSet.cycleLoUs);
  sCycling = true;
  sPhaseEndMs = millis() + dwellLo();
  if (!sArmed) escArm(true); else analogWrite(PIN_ESC_SIG, sPulseUs);
}

void escCycleStop() {
  if (!sCycling) return;
  sCycling = false;
  sAtHigh  = false;
  /* Leave the output armed at the low pulse — idle for an ESC, and far less
   * surprising than cutting the signal out from under a spinning motor. */
  escSetPulse(gSet.cycleLoUs);
}

bool escCycling()     { return sCycling; }
bool escCycleAtHigh() { return sCycling && sAtHigh; }

uint32_t escCycleRemainMs() {
  if (!sCycling) return 0;
  uint32_t now = millis();
  return (int32_t)(sPhaseEndMs - now) > 0 ? (sPhaseEndMs - now) : 0;
}

void escTick() {
  if (!sCycling) return;
  /* Signed compare so the rollover at 49.7 days is a non-event. */
  if ((int32_t)(millis() - sPhaseEndMs) < 0) return;

  sAtHigh = !sAtHigh;
  sPulseUs = clampPulse(sAtHigh ? gSet.cycleHiUs : gSet.cycleLoUs);
  analogWrite(PIN_ESC_SIG, sPulseUs);
  sPhaseEndMs += sAtHigh ? dwellHi() : dwellLo();

  /* If the UI stalled long enough to miss whole phases, resynchronise rather
   * than chasing a backlog of expired deadlines. */
  if ((int32_t)(millis() - sPhaseEndMs) > 0)
    sPhaseEndMs = millis() + (sAtHigh ? dwellHi() : dwellLo());
}
