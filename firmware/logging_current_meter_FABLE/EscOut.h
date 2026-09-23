/* ==========================================================================
 * EscOut.h — ESC servo-signal output and the motion engine behind Test Mode.
 *
 * OUTPUT. arduino-pico's bundled Servo library (PIO, writeMicroseconds, fixed
 * 20 ms / 50 Hz frame). Never analogWrite(): arduino-pico clamps
 * analogWriteFreq() to >= 100 Hz without saying so, which made v3.1's 50 Hz
 * pulses half width. The servo is attached as attach(pin, 1000, 2000, idle)
 * because a bare attach(pin) starts at 1500 us.
 *
 * ESC TYPE (gSet.escType). UNI: idle/arm 1000 us, throttle 1000-2000. BIDI:
 * neutral/arm 1500 us, reverse below. Everything that "returns to idle" goes
 * to escIdleUs(), so the same code serves both.
 *
 * STATES (EscPhase, see EscProfile.h)
 *   OFF       disarmed, pin driven LOW as GPIO
 *   MANUAL    armed, emitting the manual set point
 *   ARMING    a cycle is waiting out the 2 s idle hold
 *   PRE       Log Test pre-roll, 3 s at idle (the arming hold runs inside it)
 *   RAMP_UP / DWELL_HI / RAMP_DN / DWELL_LO   the cycle profile
 *   POST      Log Test post-roll, 5 s at idle — or with the output OFF if the
 *             test was stopped with the header STOP; the log keeps running
 *             either way so the spin-down is captured
 *
 * SAFETY
 *  - Every arm starts at idle/neutral and holds it for ESC_ARM_HOLD_MS; ESCs
 *    will not arm on a throttled first frame. The manual set point resets to
 *    idle on arm, and nothing the user sets takes effect until the hold ends.
 *  - escArm(false) — the header STOP — cuts the output in every state: at
 *    most one more pulse (an idle one) goes out, within one 20 ms frame. A running Log Test then finishes its post-roll unpowered.
 *  - The profile is snapshotted at cycle/test start, so an edit mid-run can
 *    neither change what is being emitted nor falsify the log header.
 *
 * THREADING. Core 0 only. Core 1 reads gEscOutUs into log records.
 * ========================================================================*/
#pragma once
#include <Arduino.h>
#include "EscProfile.h"

#define ESC_PULSE_MIN_US   ESC_ABS_MIN_US
#define ESC_PULSE_MAX_US   ESC_ABS_MAX_US
#define ESC_FRAME_US       20000    /* fixed by the Servo library (50 Hz)  */
#define ESC_ARM_HOLD_MS     2000    /* idle held after every arm           */

/* escTick() event flags */
#define ESC_EV_TEST_DONE   0x01     /* Log Test post-roll finished         */

/* The pulse currently on the pin in us, 0 while disarmed. */
extern volatile uint16_t gEscOutUs;

void    escBegin();                  /* pin low, disarmed. setup()          */
uint8_t escTick();                   /* every loop(); returns ESC_EV_*      */

/* ---- arming ---- */
void     escArm(bool on);            /* on: idle + hold. off: cut output now */
bool     escArmed();
bool     escHolding();
uint32_t escHoldRemainMs();
uint16_t escIdleUs();                /* 1000 UNI / 1500 BIDI                 */

/* ---- manual ---- */
void     escManualSet(uint16_t us);  /* clamped 1000-2000; ignored unless in MANUAL */
void     escManualIdle();            /* set point back to idle/neutral       */
uint16_t escManualUs();              /* the set point                        */

/* ---- cycle (continuous) ---- */
bool escCycleStart();                /* arms if needed; false if a test runs */
void escCycleStop();                 /* back to MANUAL at idle, still armed  */
bool escCycling();                   /* continuous cycle running (not test)  */

/* ---- Log Test (counted) ---- */
bool escTestStart(uint16_t cycles);  /* arms, PRE, N cycles, POST, disarm   */
void escTestAbort();                 /* jump to POST at idle, still armed   */
bool escTesting();
bool escTestAborted();

/* ---- status for the UI ---- */
EscPhase escPhase();
uint32_t escPhaseRemainMs();         /* ms left in a timed phase            */
uint16_t escCycleNum();              /* 1-based cycle in progress (0 = none) */
uint16_t escCycleTarget();           /* Log Test cycle count                */
bool     escCycleReverse();          /* BIDI: current cycle is reverse      */
const EscProfile &escRunProfile();   /* the snapshot a run is using         */
const char *escPhaseName(EscPhase p);

/* Kept for older call sites: the manual set point. */
inline uint16_t escPulse() { return escManualUs(); }

/* One-line state dump for the serial 'p' key. */
void escPrint(Print &out);
