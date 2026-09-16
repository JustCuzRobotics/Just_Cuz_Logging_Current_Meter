/* ==========================================================================
 * EscOut.h — ESC servo-signal output on PIN_ESC_SIG.
 *
 * Hardware PWM, driven through the Pico SDK directly — NOT analogWrite().
 *
 * WHY NOT analogWrite (the v3.1 bug). arduino-pico's analogWriteFreq() clamps
 * anything under 100 Hz up to 100 Hz, silently (the warning is DEBUGCORE
 * only). v3.1 asked for 50 Hz with analogWriteRange(20000) so that the duty
 * value would read as microseconds; the core ran the slice at 100 Hz instead,
 * so a 20000-count range spanned a 10 ms frame and every pulse came out at
 * HALF width — "1000 us idle" was 500 us, "2000 us" was 1000 us. Any frame
 * longer than 10 ms was scaled by 10000/period. It also re-initialised the
 * slice (resetting its counter mid-frame) on every applyPwm(), because the
 * requested 50 never equalled the stored, clamped 100. Some ESCs reject a
 * 500 us pulse as an invalid signal and never arm; others read it as zero
 * throttle. That is the "one ESC arms, the other doesn't" bench report.
 *
 * THE FIX. The slice is clocked at exactly 1 MHz (divider = clk_sys / 1 MHz,
 * an integer at the default 133 MHz and at 125 MHz), so one count is one
 * microsecond: TOP = period - 1, CC = pulse. No global analogWrite state is
 * involved, nothing is ever re-initialised while armed, and the RP2040 latches
 * TOP and CC at the end of a period — so pulse and period changes can never
 * produce a runt pulse. escPrintPwm() reads the slice registers back so a
 * scope measurement has something to be compared against.
 *
 * SAFETY.
 *  - OFF at boot: escBegin() drives the pin LOW as plain GPIO; the PWM slice
 *    is not engaged, so a board that resets mid-test does not come back
 *    spinning.
 *  - ARMING ALWAYS STARTS AT IDLE. escArm(true) resets the set point to
 *    ESC_PULSE_MIN_US and holds it for ESC_ARM_HOLD_MS before anything else
 *    is emitted. ESCs refuse to arm if their first frames are above zero
 *    throttle, so a set point stepped up before arming (or the old serial
 *    'e' key's 1500 us) would look exactly like a dead ESC. Stepper presses
 *    during the hold update the set point; it is applied when the hold ends.
 *    A cycle started while disarmed arms, holds, then begins at its low end.
 *  - Disarming returns the pin to a plain LOW output rather than a 0 us
 *    pulse, because a zero-width frame is not a defined servo signal.
 *
 * THREADING. Core 0 only — escTick() is driven from the main loop and the
 * setters are called from touch handlers that already run there. Core 1 only
 * ever READS gEscOutUs, to stamp log records with what was on the pin.
 * ========================================================================*/
#pragma once
#include <Arduino.h>

#define ESC_PULSE_MIN_US   1000     /* idle / zero throttle                */
#define ESC_PULSE_MAX_US   2000
#define ESC_PERIOD_MIN_US   5000    /* 200 Hz — as fast as any ESC wants   */
#define ESC_PERIOD_MAX_US  20000    /*  50 Hz — the classic servo frame    */
#define ESC_ARM_HOLD_MS     2000    /* idle held after every arm           */

/* The pulse currently on the pin in us, 0 while disarmed. Core 0 writes it
 * whenever the PWM level changes; core 1 reads it into each log record. */
extern volatile uint16_t gEscOutUs;

void escBegin();          /* pin low, output disarmed. Call from setup()   */
void escTick();           /* arm hold + auto-cycle. Call every loop()      */

/* ---- manual control ---- */
void     escArm(bool on); /* true: idle + hold. false also stops any cycle */
bool     escArmed();
bool     escHolding();                /* armed, still inside the idle hold */
uint32_t escHoldRemainMs();
void     escSetPulse(uint16_t us);    /* clamped to ESC_PULSE_MIN/MAX_US   */
uint16_t escPulse();                  /* the set point                     */
void     escSetPeriod(uint16_t us);   /* clamped; latched at frame end     */
uint16_t escPeriod();

/* ---- auto-cycle ----
 * Alternates between gSet.cycleLoUs and gSet.cycleHiUs, dwelling cycleLoMs
 * and cycleHiMs at each end. Starting a cycle arms the output (with the idle
 * hold if it was disarmed); stopping it leaves the output armed at the low
 * pulse, which for an ESC is idle. */
void escCycleStart();
void escCycleStop();
bool escCycling();
bool escCycleAtHigh();    /* true while dwelling at the high pulse         */
uint32_t escCycleRemainMs();   /* ms left in the current dwell, for the UI */

/* Slice register readback, for checking against a scope. Core 0. */
void escPrintPwm(Print &out);
