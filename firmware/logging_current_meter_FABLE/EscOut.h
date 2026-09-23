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
 * neutral/arm 1500 us, reverse below (if the ESC itself is in its
 * bidirectional/3D mode). Everything that "returns to idle" goes
 * to escIdleUs(), so the same code serves both.
 *
 * STATES (EscPhase, see EscProfile.h)
 *   OFF       disarmed, pin driven LOW as GPIO
 *   MANUAL    armed, emitting the manual set point
 *   PRE       pre-roll at idle before EVERY run, cycle or Log Test, as long
 *             as gSet.preRollMs (the 2 s arming hold runs inside it). An ESC
 *             ignores throttle until it has finished starting up, so a run
 *             that began the moment the signal appeared would jerk into its
 *             first ramp
 *   RAMP_UP / DWELL_HI / RAMP_DN / DWELL_LO   the cycle profile
 *   POST      Log Test post-roll, 5 s at idle — or with the output OFF if the
 *             test was stopped with the header STOP; the log keeps running
 *             either way so the spin-down is captured
 *
 * STOPPING — two levels, because they are different needs.
 *   escNeutral()  the output stays live and goes to idle/neutral, stopping a
 *                 running cycle or aborting a test. The motor stops at once,
 *                 and the next run starts without waiting out the ESC's
 *                 start-up again. This is what a tap of the header button
 *                 does, and where a finished run ends up.
 *   escCut()      the output goes away entirely (pin driven LOW). A held
 *                 header button, the serial key, and anything that wants the
 *                 pin genuinely dead. At most one more pulse — an idle one —
 *                 goes out, within one 20 ms frame. A Log Test cut this way
 *                 still finishes its post-roll unpowered so the log captures
 *                 the spin-down.
 * Whenever the output ends up live at neutral with nothing running, a fuse
 * runs: ESC_IDLE_CUT_MS after a run finished on its own, the much longer
 * ESC_IDLE_CUT_MANUAL_MS after a stop the operator asked for (they are
 * standing there, and cutting it would cost them the ESC's start-up again).
 * Either way a bench that is walked away from goes quiet by itself. Touching
 * the throttle, or starting anything, cancels the fuse.
 *
 * SAFETY
 *  - Every arm starts at idle/neutral and holds it for ESC_ARM_HOLD_MS; ESCs
 *    will not arm on a throttled first frame. The manual set point resets to
 *    idle on arm, and nothing the user sets takes effect until the hold ends.
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
#define ESC_IDLE_CUT_MS    30000    /* live-at-neutral fuse after a run     */
#define ESC_IDLE_CUT_MANUAL_MS 300000  /* ...and after a stop asked for by hand */

/* escTick() event flags */
#define ESC_EV_TEST_DONE   0x01     /* Log Test post-roll finished         */

/* The pulse currently on the pin in us, 0 while disarmed. */
extern volatile uint16_t gEscOutUs;

void    escBegin();                  /* pin low, disarmed. setup()          */
uint8_t escTick();                   /* every loop(); returns ESC_EV_*      */

/* ---- arming and stopping ---- */
void     escArm(bool on);            /* on: idle + hold. off: same as escCut */
void     escCut();                   /* output off now, in every state       */
bool     escNeutral();               /* stay live at idle; stops a run       */
bool     escAtNeutral();             /* armed, idle on the pin, nothing running */
uint32_t escCutRemainMs();           /* fuse left after a run, 0 if not armed */
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
bool escTestStart(uint16_t cycles);  /* arms if needed, PRE, N cycles, POST  */
void escTestAbort();                 /* jump to POST at idle, still armed   */
bool escTesting();
bool escTestAborted();

/* ---- status for the UI ---- */
EscPhase escPhase();
uint32_t escPhaseRemainMs();         /* ms left in a timed phase            */
uint16_t escCycleNum();              /* 1-based cycle in progress (0 = none) */
uint16_t escCycleTarget();           /* Log Test cycle count                */
bool     escOutIsReverse();          /* BIDI: the pulse is below neutral    */
const EscProfile &escRunProfile();   /* the snapshot a run is using         */
const char *escPhaseName(EscPhase p);

/* Kept for older call sites: the manual set point. */
inline uint16_t escPulse() { return escManualUs(); }

/* One-line state dump for the serial 'p' key. */
void escPrint(Print &out);
