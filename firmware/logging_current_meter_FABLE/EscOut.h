/* ==========================================================================
 * EscOut.h — ESC servo-signal output on PIN_ESC_SIG.
 *
 * Hardware PWM, not bit-banging. display_bringup's escPulseTest() built its
 * frame with delayMicroseconds(), which would stall whichever core ran it and
 * break the dual-core contract outright. An RP2040 PWM slice produces the
 * same waveform with zero CPU:
 *
 *     analogWriteFreq(1000000UL / periodUs);   // 50 Hz for a 20 ms frame
 *     analogWriteRange(periodUs);              // range in us...
 *     analogWrite(PIN_ESC_SIG, pulseUs);       // ...so the value IS the pulse
 *
 * Choosing the range to equal the period in microseconds is what makes the
 * duty value read directly as a pulse width, which keeps every call site in
 * the units the user actually thinks in.
 *
 * SAFETY. The output is OFF at boot and escBegin() drives the pin low without
 * ever engaging the PWM slice, so a board that resets mid-test does not come
 * back spinning. Disarming returns the pin to a plain LOW output rather than
 * a 0 us pulse, because a zero-width frame is not a defined servo signal.
 *
 * THREADING. Core 0 only — escTick() is driven from the main loop and the
 * setters are called from touch handlers that already run there. Core 1 never
 * touches this module. The auto-cycle is a millis() state machine that only
 * ever changes the PWM duty, so it never blocks and its timing does not
 * depend on the UI frame rate.
 * ========================================================================*/
#pragma once
#include <Arduino.h>

#define ESC_PULSE_MIN_US   1000
#define ESC_PULSE_MAX_US   2000
#define ESC_PERIOD_MIN_US   5000    /* 200 Hz — as fast as any ESC wants   */
#define ESC_PERIOD_MAX_US  20000    /*  50 Hz — the classic servo frame    */

void escBegin();          /* pin low, output disarmed. Call from setup()   */
void escTick();           /* advances the auto-cycle. Call every loop()    */

/* ---- manual control ---- */
void     escArm(bool on); /* false also stops any running cycle            */
bool     escArmed();
void     escSetPulse(uint16_t us);    /* clamped to ESC_PULSE_MIN/MAX_US   */
uint16_t escPulse();                  /* the pulse currently being emitted */
void     escSetPeriod(uint16_t us);   /* clamped; re-applies immediately   */
uint16_t escPeriod();

/* ---- auto-cycle ----
 * Alternates between gSet.cycleLoUs and gSet.cycleHiUs, dwelling cycleLoMs
 * and cycleHiMs at each end. Starting a cycle arms the output; stopping it
 * leaves the output armed at the low pulse, which for an ESC is idle. */
void escCycleStart();
void escCycleStop();
bool escCycling();
bool escCycleAtHigh();    /* true while dwelling at the high pulse         */
uint32_t escCycleRemainMs();   /* ms left in the current dwell, for the UI */
