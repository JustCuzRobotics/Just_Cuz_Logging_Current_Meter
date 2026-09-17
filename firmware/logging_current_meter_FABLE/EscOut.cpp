/* ==========================================================================
 * EscOut.cpp — see EscOut.h for the design rationale.
 * ========================================================================*/
#include "EscOut.h"
#include "Config.h"
#include "Settings.h"
#include <Servo.h>
#include <hardware/gpio.h>

volatile uint16_t gEscOutUs = 0;

static Servo    sServo;
static bool     sArmed    = false;
static bool     sHolding  = false;
static bool     sCycling  = false;
static bool     sAtHigh   = false;
static uint16_t sPulseUs  = ESC_PULSE_MIN_US;   /* set point               */
static uint32_t sHoldEndMs  = 0;
static uint32_t sPhaseEndMs = 0;

/* ---- helpers ---------------------------------------------------------- */

static uint16_t clampPulse(uint16_t us) {
  if (us < ESC_PULSE_MIN_US) return ESC_PULSE_MIN_US;
  if (us > ESC_PULSE_MAX_US) return ESC_PULSE_MAX_US;
  return us;
}

/* Only ever called while armed. The library clears any not-yet-used value
 * from the PIO FIFO, so the newest set point is what the next frame carries. */
static void setLevel(uint16_t us) {
  sServo.writeMicroseconds(us);
  gEscOutUs = us;
}

static void pinLow() {
  /* Value and direction first, then hand the pin to SIO, so it goes straight
   * to a driven LOW rather than passing through a floating input. */
  gpio_put(PIN_ESC_SIG, 0);
  gpio_set_dir(PIN_ESC_SIG, GPIO_OUT);
  gpio_set_function(PIN_ESC_SIG, GPIO_FUNC_SIO);
  gEscOutUs = 0;
}

/* A dwell of 0 ms would spin the state machine every tick, so both dwells are
 * floored at one tick's worth of time. */
static uint32_t dwellLo() { return gSet.cycleLoMs ? gSet.cycleLoMs : 1; }
static uint32_t dwellHi() { return gSet.cycleHiMs ? gSet.cycleHiMs : 1; }

static void beginCycleLowPhase() {
  sAtHigh  = false;
  sPulseUs = clampPulse(gSet.cycleLoUs);
  setLevel(sPulseUs);
  sPhaseEndMs = millis() + dwellLo();
}

/* ---- lifecycle -------------------------------------------------------- */

void escBegin() {
  sArmed = sHolding = sCycling = sAtHigh = false;
  sPulseUs = ESC_PULSE_MIN_US;            /* never boot anywhere but idle    */
  pinLow();
}

/* ---- manual ----------------------------------------------------------- */

void escArm(bool on) {
  if (on == sArmed) return;
  if (on) {
    sPulseUs = ESC_PULSE_MIN_US;          /* set point resets to idle        */
    /* Explicit initial value: attach(pin) alone would start at 1500 us. */
    if (sServo.attach(PIN_ESC_SIG, ESC_PULSE_MIN_US, ESC_PULSE_MAX_US, ESC_PULSE_MIN_US) < 0) {
      pinLow();                           /* no free PIO state machine       */
      return;
    }
    sArmed   = true;
    sHolding = true;
    sHoldEndMs = millis() + ESC_ARM_HOLD_MS;
    gEscOutUs = ESC_PULSE_MIN_US;
  } else {
    sArmed = sHolding = sCycling = sAtHigh = false;
    sServo.detach();                      /* finishes the frame in progress  */
    pinLow();
  }
}

bool escArmed()   { return sArmed; }
bool escHolding() { return sArmed && sHolding; }

uint32_t escHoldRemainMs() {
  if (!escHolding()) return 0;
  int32_t r = (int32_t)(sHoldEndMs - millis());
  return r > 0 ? (uint32_t)r : 0;
}

void escSetPulse(uint16_t us) {
  sPulseUs = clampPulse(us);
  /* During the hold the new set point is only remembered; the hold ends by
   * applying it. During a cycle the cycle owns the pin. */
  if (sArmed && !sHolding && !sCycling) setLevel(sPulseUs);
}

uint16_t escPulse() { return sPulseUs; }

/* ---- auto-cycle ------------------------------------------------------- */

void escCycleStart() {
  if (sCycling) return;
  if (!sArmed) {
    escArm(true);                 /* idle hold first; escTick starts the low phase */
    if (!sArmed) return;
    sCycling = true;
    sAtHigh  = false;
  } else {
    sCycling = true;
    sAtHigh  = false;
    if (!sHolding) beginCycleLowPhase();
  }
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
bool escCycleAtHigh() { return sCycling && sAtHigh && !sHolding; }

uint32_t escCycleRemainMs() {
  if (!sCycling) return 0;
  if (sHolding) return escHoldRemainMs();
  int32_t r = (int32_t)(sPhaseEndMs - millis());
  return r > 0 ? (uint32_t)r : 0;
}

void escTick() {
  if (!sArmed) return;

  if (sHolding) {
    /* Signed compare so the rollover at 49.7 days is a non-event. */
    if ((int32_t)(millis() - sHoldEndMs) < 0) return;
    sHolding = false;
    if (sCycling) beginCycleLowPhase();
    else          setLevel(sPulseUs);
    return;
  }

  if (!sCycling) return;
  if ((int32_t)(millis() - sPhaseEndMs) < 0) return;

  sAtHigh = !sAtHigh;
  sPulseUs = clampPulse(sAtHigh ? gSet.cycleHiUs : gSet.cycleLoUs);
  setLevel(sPulseUs);
  sPhaseEndMs += sAtHigh ? dwellHi() : dwellLo();

  /* If the UI stalled long enough to miss whole phases, resynchronise rather
   * than chasing a backlog of expired deadlines. */
  if ((int32_t)(millis() - sPhaseEndMs) > 0)
    sPhaseEndMs = millis() + (sAtHigh ? dwellHi() : dwellLo());
}

void escPrint(Print &out) {
  char line[160];
  snprintf(line, sizeof line,
           "# [esc] GP%u servo(PIO) %s  frame %u us (50 Hz fixed)  set=%u us  on pin=%u us%s%s\n",
           (unsigned)PIN_ESC_SIG, sServo.attached() ? "attached" : "detached",
           (unsigned)ESC_FRAME_US, (unsigned)sPulseUs, (unsigned)gEscOutUs,
           sHolding && sArmed ? "  ARM HOLD" : "", sCycling ? "  CYCLE" : "");
  out.print(line);
}
