/* ==========================================================================
 * EscOut.cpp — see EscOut.h for the design rationale, including why this no
 * longer uses analogWrite().
 * ========================================================================*/
#include "EscOut.h"
#include "Config.h"
#include "Settings.h"
#include <hardware/pwm.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>

volatile uint16_t gEscOutUs = 0;

static bool     sArmed    = false;
static bool     sHolding  = false;
static bool     sCycling  = false;
static bool     sAtHigh   = false;
static uint16_t sPulseUs  = ESC_PULSE_MIN_US;   /* set point               */
static uint16_t sPeriodUs = 20000;
static uint32_t sHoldEndMs  = 0;
static uint32_t sPhaseEndMs = 0;

static inline uint8_t slice()   { return (uint8_t)pwm_gpio_to_slice_num(PIN_ESC_SIG); }
static inline uint8_t channel() { return (uint8_t)pwm_gpio_to_channel(PIN_ESC_SIG); }

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

/* Only ever called while armed. CC is latched by the RP2040 at the end of the
 * current period, so this cannot shorten or split a frame in progress. */
static void setLevel(uint16_t us) {
  pwm_set_chan_level(slice(), channel(), us);
  gEscOutUs = us;
}

/* One-time slice setup on arm: 1 us per count. The divider is 8.4 fixed
 * point, so it is computed in sixteenths and rounded — exact whenever clk_sys
 * is a whole number of MHz, which covers every clock arduino-pico uses. */
static void pwmStart(uint16_t firstPulseUs) {
  uint8_t s = slice();
  uint32_t sysHz = clock_get_hz(clk_sys);
  uint32_t div16 = (uint32_t)(((uint64_t)sysHz * 16ULL + 500000ULL) / 1000000ULL);
  pwm_set_enabled(s, false);
  pwm_set_clkdiv_int_frac(s, (uint8_t)(div16 >> 4), (uint8_t)(div16 & 0x0F));
  pwm_set_wrap(s, (uint16_t)(sPeriodUs - 1));
  pwm_set_chan_level(s, channel(), firstPulseUs);
  pwm_set_counter(s, 0);                 /* first frame is a whole frame     */
  gpio_set_function(PIN_ESC_SIG, GPIO_FUNC_PWM);
  pwm_set_enabled(s, true);
  gEscOutUs = firstPulseUs;
}

static void pwmStop() {
  /* Output value and direction first, THEN hand the pin back to SIO, so it
   * goes straight from PWM to a driven LOW. (gpio_init() would pass through
   * a high-impedance input on the way.) Then stop the slice. */
  gpio_put(PIN_ESC_SIG, 0);
  gpio_set_dir(PIN_ESC_SIG, GPIO_OUT);
  gpio_set_function(PIN_ESC_SIG, GPIO_FUNC_SIO);
  pwm_set_enabled(slice(), false);
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
  sPulseUs  = ESC_PULSE_MIN_US;           /* never boot anywhere but idle    */
  sPeriodUs = clampPeriod(gSet.escPeriodUs);
  pwmStop();
}

/* ---- manual ----------------------------------------------------------- */

void escArm(bool on) {
  if (on == sArmed) return;
  if (on) {
    sArmed   = true;
    sHolding = true;
    sPulseUs = ESC_PULSE_MIN_US;          /* set point resets to idle        */
    sHoldEndMs = millis() + ESC_ARM_HOLD_MS;
    pwmStart(ESC_PULSE_MIN_US);
  } else {
    sArmed = sHolding = sCycling = sAtHigh = false;
    pwmStop();
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

void escSetPeriod(uint16_t us) {
  sPeriodUs = clampPeriod(us);
  if (sArmed) pwm_set_wrap(slice(), (uint16_t)(sPeriodUs - 1));   /* latched */
}

uint16_t escPeriod() { return sPeriodUs; }

/* ---- auto-cycle ------------------------------------------------------- */

void escCycleStart() {
  if (sCycling) return;
  sCycling = true;
  sAtHigh  = false;
  if (!sArmed) {
    escArm(true);                 /* idle hold first; escTick starts the low phase */
  } else if (!sHolding) {
    beginCycleLowPhase();
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

/* ---- scope check ------------------------------------------------------ */

void escPrintPwm(Print &out) {
  uint8_t s = slice();
  uint32_t div  = pwm_hw->slice[s].div;          /* 8.4 fixed point          */
  uint32_t top  = pwm_hw->slice[s].top;
  uint32_t cc   = pwm_hw->slice[s].cc;
  uint32_t csr  = pwm_hw->slice[s].csr;
  uint32_t lvl  = channel() ? ((cc >> 16) & 0xFFFF) : (cc & 0xFFFF);
  uint32_t sysHz = clock_get_hz(clk_sys);
  /* tick = div / sysHz; period = (top+1) ticks. In ns to stay integer. */
  uint64_t tickNs16 = (uint64_t)div * 1000000000ULL / sysHz;   /* x16      */
  uint32_t periodUs = (uint32_t)(tickNs16 * (top + 1) / 16ULL / 1000ULL);
  uint32_t pulseUs  = (uint32_t)(tickNs16 * lvl / 16ULL / 1000ULL);
  char line[320];
  snprintf(line, sizeof line,
           "# [pwm] GP%u slice %u ch %c  en=%lu  clk_sys=%lu Hz  div=%lu+%lu/16  top=%lu  level=%lu\n"
           "# [pwm] => period %lu us (%lu.%lu Hz), pulse %lu us   | state: %s%s%s set=%u out=%u\n",
           (unsigned)PIN_ESC_SIG, (unsigned)s, channel() ? 'B' : 'A',
           (unsigned long)(csr & 1), (unsigned long)sysHz,
           (unsigned long)(div >> 4), (unsigned long)(div & 0xF),
           (unsigned long)top, (unsigned long)lvl,
           (unsigned long)periodUs,
           (unsigned long)(periodUs ? 1000000UL / periodUs : 0),
           (unsigned long)(periodUs ? (10000000UL / periodUs) % 10 : 0),
           (unsigned long)pulseUs,
           sArmed ? "ARMED" : "off", sHolding ? " HOLD" : "", sCycling ? " CYCLE" : "",
           (unsigned)sPulseUs, (unsigned)gEscOutUs);
  out.print(line);
}
