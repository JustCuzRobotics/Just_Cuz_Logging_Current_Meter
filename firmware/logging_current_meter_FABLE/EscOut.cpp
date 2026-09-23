/* ==========================================================================
 * EscOut.cpp — see EscOut.h. The profile maths lives in EscProfile.h.
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
static uint32_t sHoldEndMs = 0;

static EscPhase sPhase     = PH_OFF;
static uint32_t sPhaseStartMs = 0;
static bool     sTest      = false;     /* the run is a Log Test            */
static bool     sAborted   = false;
static uint16_t sCycleIdx  = 0;         /* completed cycles                 */
static uint16_t sCycleTarget = 0;
static uint16_t sManualUs  = ESC_UNI_IDLE_US;
static EscProfile sRun;                 /* snapshot at run start            */
static uint8_t  sRunType   = ESC_TYPE_UNI;
/* When a run ends the output stays live at neutral so the next one starts
 * instantly. This is the fuse that cuts it if nothing then happens. 0 = no
 * fuse running (manual work, or already cut). */
static uint32_t sCutAtMs   = 0;

/* ---- helpers ---------------------------------------------------------- */

static uint16_t clampUs(int32_t us) {
  if (us < ESC_ABS_MIN_US) return ESC_ABS_MIN_US;
  if (us > ESC_ABS_MAX_US) return ESC_ABS_MAX_US;
  return (uint16_t)us;
}

uint16_t escIdleUs() { return escTypeIdleUs(gSet.escType); }

static void pinLow() {
  /* Value and direction first, then hand the pin to SIO, so it goes straight
   * to a driven LOW rather than passing through a floating input. */
  gpio_put(PIN_ESC_SIG, 0);
  gpio_set_dir(PIN_ESC_SIG, GPIO_OUT);
  gpio_set_function(PIN_ESC_SIG, GPIO_FUNC_SIO);
  gEscOutUs = 0;
}

static void setLevel(uint16_t us) {
  if (!sArmed || us == gEscOutUs) return;
  sServo.writeMicroseconds(us);          /* library drops any stale FIFO value */
  gEscOutUs = us;
}

static void outputOff() {
  if (sServo.attached()) {
    /* detach() queues a halt without clearing the FIFO, so a value queued
     * earlier this frame would still go out once. Replacing it with idle
     * first makes that last possible pulse a harmless idle one. */
    sServo.writeMicroseconds(escIdleUs());
    sServo.detach();                      /* lets the frame in progress finish */
  }
  pinLow();
  sArmed = false;
  sHolding = false;
}

static bool outputOn() {
  uint16_t idle = escIdleUs();
  /* Explicit initial value: attach(pin) alone would start at 1500 us. */
  if (sServo.attach(PIN_ESC_SIG, ESC_ABS_MIN_US, ESC_ABS_MAX_US, idle) < 0) {
    pinLow();                           /* no free PIO state machine        */
    return false;
  }
  sArmed = true;
  sHolding = true;
  sHoldEndMs = millis() + ESC_ARM_HOLD_MS;
  gEscOutUs = idle;
  return true;
}

static void enterPhase(EscPhase p) {
  sPhase = p;
  sPhaseStartMs = millis();
}

static void snapshotProfile() {
  sRun.preMs     = gSet.preRollMs;
  sRun.lowUs     = gSet.profLowUs;
  sRun.highUs    = gSet.profHighUs;
  sRun.rampUpMs  = gSet.profRampUpMs;
  sRun.dwellHiMs = gSet.profDwellHiMs;
  sRun.rampDnMs  = gSet.profRampDnMs;
  sRun.dwellLoMs = gSet.profDwellLoMs;
  sRunType = gSet.escType;
  sRun.mode = gSet.cycleMode;
}

/* First cycle of a run. Called from the tick when the pre-roll ends, so the
 * phase start is fixed up by the caller's schedule bookkeeping. */
static void startCycle0() {
  sCycleIdx = 0;
  sPhase = PH_RAMP_UP;
}

/* ---- lifecycle -------------------------------------------------------- */

void escBegin() {
  sArmed = sHolding = sTest = sAborted = false;
  sCutAtMs = 0;
  sPhase = PH_OFF;
  sManualUs = escIdleUs();                /* never boot anywhere but idle    */
  pinLow();
}

/* ---- arming ----------------------------------------------------------- */

void escArm(bool on) {
  if (!on) { escCut(); return; }
  if (sArmed) { sCutAtMs = 0; return; }   /* already live: just cancel the fuse */
  if (sTest) return;                      /* no re-arm into a test's post-roll */
  if (!outputOn()) return;
  sManualUs = escIdleUs();                /* set point resets to idle        */
  sCutAtMs = 0;
  if (sPhase == PH_OFF) enterPhase(PH_MANUAL);
}

void escCut() {
  outputOff();
  sCutAtMs = 0;
  if (sTest && sPhase != PH_POST) {
    /* Keep the log going through an unpowered post-roll. */
    sAborted = true;
    enterPhase(PH_POST);
  } else if (!sTest) {
    enterPhase(PH_OFF);
  }
}

/* millis() + a timeout can legally land on 0, which is also "no fuse", so it
 * is nudged by a millisecond. */
static void armFuse(uint32_t ms) {
  sCutAtMs = millis() + ms;
  if (!sCutAtMs) sCutAtMs = 1;
}

/* Stop whatever is happening but leave the signal up at idle/neutral. The
 * motor stops immediately — the idle pulse is pushed out here rather than
 * waiting for the next escTick(), because a caller may block for a long time
 * straight afterwards (opening a log can mount a card, which takes seconds).
 *
 * Returns false when there is nothing it can do: with the output already off,
 * or during a test's post-roll, where the only remaining escalation is a cut.
 * The header button relies on that to fall through to escCut(). */
bool escNeutral() {
  if (!sArmed) return false;
  if (sTest) {
    if (sPhase == PH_POST) return false;
    escTestAbort();
    setLevel(escIdleUs());
    return true;
  }
  if (escCycling()) { escCycleStop(); return true; }
  sManualUs = escIdleUs();
  if (sPhase != PH_MANUAL) enterPhase(PH_MANUAL);
  setLevel(escIdleUs());
  /* A stop the operator asked for by hand leaves the output live, because
   * that is the point — but not forever: a much longer fuse than the one a
   * finished run gets still catches a bench that is simply walked away from. */
  armFuse(ESC_IDLE_CUT_MANUAL_MS);
  return true;
}

bool escAtNeutral() {
  return sArmed && !sTest && !escCycling() && gEscOutUs == escIdleUs();
}

uint32_t escCutRemainMs() {
  if (!sArmed || !sCutAtMs) return 0;
  int32_t r = (int32_t)(sCutAtMs - millis());
  return r > 0 ? (uint32_t)r : 0;
}

bool escArmed()   { return sArmed; }
bool escHolding() { return sArmed && sHolding; }

uint32_t escHoldRemainMs() {
  if (!escHolding()) return 0;
  int32_t r = (int32_t)(sHoldEndMs - millis());
  return r > 0 ? (uint32_t)r : 0;
}

/* ---- manual ----------------------------------------------------------- */

/* Touching the throttle is the operator being present, so it cancels the
 * post-run fuse. */
void escManualSet(uint16_t us) {
  sManualUs = clampUs(us);
  sCutAtMs = 0;
}
void escManualIdle() { sManualUs = escIdleUs(); }
uint16_t escManualUs() { return sManualUs; }

/* ---- cycle / test ----------------------------------------------------- */

/* Starting a cycle works whether the output is already live or not: an armed
 * ESC is simply commanded to idle for the pre-roll rather than being refused,
 * which is what the operator meant by pressing START. */
bool escCycleStart() {
  if (sTest) return false;
  if (!sArmed && !outputOn()) return false;
  snapshotProfile();
  sAborted = false;
  sCycleTarget = 0;
  sCutAtMs = 0;
  sManualUs = escIdleUs();                /* the pre-roll sits at idle      */
  enterPhase(PH_PRE);
  setLevel(escIdleUs());                  /* now: START means stop moving   */
  return true;
}

void escCycleStop() {
  if (sTest) return;
  sManualUs = escIdleUs();
  if (sArmed) {
    enterPhase(PH_MANUAL);
    setLevel(escIdleUs());           /* now, not at the next tick           */
    armFuse(ESC_IDLE_CUT_MS);
  } else {
    enterPhase(PH_OFF);
  }
}

bool escCycling() {
  return !sTest && (sPhase == PH_PRE || sPhase == PH_ENTRY || sPhase == PH_RAMP_UP ||
                    sPhase == PH_DWELL_HI || sPhase == PH_RAMP_DN || sPhase == PH_DWELL_LO);
}

bool escTestStart(uint16_t cycles) {
  if (sTest || cycles == 0) return false;
  if (!sArmed && !outputOn()) return false;
  snapshotProfile();
  sTest = true;
  sAborted = false;
  sCycleTarget = cycles;
  sCycleIdx = 0;
  sCutAtMs = 0;
  sManualUs = escIdleUs();
  enterPhase(PH_PRE);                      /* the 2 s hold runs inside it     */
  setLevel(escIdleUs());                   /* before the caller opens a log   */
  return true;
}

void escTestAbort() {
  if (!sTest || sPhase == PH_POST) return;
  sAborted = true;
  enterPhase(PH_POST);
}

bool escTesting()     { return sTest; }
bool escTestAborted() { return sAborted; }

/* ---- tick ------------------------------------------------------------- */

uint8_t escTick() {
  uint8_t ev = 0;
  uint32_t now = millis();

  if (sHolding && (int32_t)(now - sHoldEndMs) >= 0) sHolding = false;

  /* The live-at-neutral fuse. Anything the operator does cancels it; only
   * being left alone lets it run out. */
  if (sArmed && sCutAtMs && (int32_t)(now - sCutAtMs) >= 0) {
    escCut();
    return ev;
  }

  /* Advance timed phases. The loop handles 0 ms ramps (several transitions in
   * one tick); phase starts advance by the nominal length so the schedule
   * does not drift, unless the UI stalled far past a boundary. */
  for (uint8_t guard = 0; guard < 8; guard++) {
    if (sPhase < PH_PRE) break;            /* OFF, MANUAL, ARMING: untimed    */
    uint32_t len = profilePhaseMs(sPhase, sRun);
    uint32_t el = now - sPhaseStartMs;
    if (sPhase == PH_PRE && sHolding) break;   /* PRE never ends inside the hold */
    if (el < len) break;
    /* Keep the nominal schedule through ordinary loop jitter, but resync
     * after a real stall: catching up would skip a short dwell entirely. */
    uint32_t next = (el - len > 50) ? now : sPhaseStartMs + len;

    switch (sPhase) {
      /* The pre-roll ends at idle. If the cycle's low end is somewhere else
       * (SPIN -> SPIN), ease into it over the ramp-up time rather than
       * stepping — the ESC has just finished arming and WILL respond. */
      case PH_PRE:
        if (profileLowUs(sRun, sRunType) != escTypeIdleUs(sRunType) && sRun.rampUpMs)
          sPhase = PH_ENTRY;
        else
          startCycle0();
        break;
      case PH_ENTRY:    startCycle0(); break;
      case PH_RAMP_UP:  sPhase = PH_DWELL_HI; break;
      case PH_DWELL_HI: sPhase = PH_RAMP_DN;  break;
      case PH_RAMP_DN:  sPhase = PH_DWELL_LO; break;
      case PH_DWELL_LO:
        sCycleIdx++;
        if (sTest && sCycleIdx >= sCycleTarget) {
          sPhase = PH_POST;
        } else {
          sPhase = PH_RAMP_UP;
        }
        break;
      case PH_POST:
        /* Test complete. If the output is still live it stays live, at
         * neutral, so a second test can start straight away — with the fuse
         * running in case nobody does. A test that was cut mid-run is already
         * off and stays off. */
        sTest = false;
        if (sArmed) {
          sManualUs = escIdleUs();
          sPhase = PH_MANUAL;
          armFuse(ESC_IDLE_CUT_MS);
        } else {
          sPhase = PH_OFF;
        }
        ev |= ESC_EV_TEST_DONE;
        break;
      default: break;
    }
    sPhaseStartMs = next;
    if (sPhase == PH_OFF || sPhase == PH_MANUAL) break;
  }

  /* Drive the pin. */
  if (sArmed) {
    uint16_t us;
    if (sHolding)               us = escIdleUs();
    else if (sPhase == PH_MANUAL) us = sManualUs;
    else if (sPhase == PH_ENTRY || (sPhase >= PH_RAMP_UP && sPhase <= PH_DWELL_LO))
      us = profilePulse(sPhase, now - sPhaseStartMs, sRun, sRunType);
    else                        us = escTypeIdleUs(sRunType);   /* ARMING/PRE/POST */
    setLevel(clampUs(us));
  }
  return ev;
}

/* ---- status ----------------------------------------------------------- */

EscPhase escPhase() { return sPhase; }

uint32_t escPhaseRemainMs() {
  if (sPhase < PH_PRE) return 0;
  uint32_t len = profilePhaseMs(sPhase, sRun);
  uint32_t el = millis() - sPhaseStartMs;
  uint32_t r = el < len ? len - el : 0;
  if (sPhase == PH_PRE && escHoldRemainMs() > r) r = escHoldRemainMs();
  return r;
}

uint16_t escCycleNum() {
  if (sPhase >= PH_RAMP_UP && sPhase <= PH_DWELL_LO) return (uint16_t)(sCycleIdx + 1);
  if (sPhase == PH_POST) return sCycleIdx;
  return 0;
}
uint16_t escCycleTarget()  { return sCycleTarget; }
/* Display only: a bidirectional ESC in its 3D mode runs backwards below
 * neutral, so anything under 1500 is shown as REV. Nothing in the engine
 * cares — the profile is plain pulse values. */
bool     escOutIsReverse() {
  /* The LIVE ESC type, not the run snapshot: this is a label for whatever is
   * on the pin right now, including a manual set point with no run at all. */
  return sArmed && gSet.escType == ESC_TYPE_BIDI && gEscOutUs && gEscOutUs < ESC_BIDI_IDLE_US;
}
const EscProfile &escRunProfile() { return sRun; }

const char *escPhaseName(EscPhase p) {
  switch (p) {
    case PH_OFF:      return "OFF";
    case PH_MANUAL:   return "MANUAL";
    case PH_PRE:      return "PRE-ROLL";
    case PH_ENTRY:    return "LEAD-IN";
    case PH_RAMP_UP:  return "RAMP UP";
    case PH_DWELL_HI: return "DWELL HI";
    case PH_RAMP_DN:  return "RAMP DN";
    case PH_DWELL_LO: return "DWELL LO";
    case PH_POST:     return "POST-ROLL";
    default:          return "?";
  }
}

void escPrint(Print &out) {
  char line[200];
  snprintf(line, sizeof line,
           "# [esc] GP%u servo(PIO) %s  %s idle=%u  frame %u us  phase=%s%s  set=%u  on pin=%u  cycle %u/%u%s\n",
           (unsigned)PIN_ESC_SIG, sServo.attached() ? "attached" : "detached",
           gSet.escType == ESC_TYPE_BIDI ? "BIDI" : "UNI", (unsigned)escIdleUs(),
           (unsigned)ESC_FRAME_US, escPhaseName(sPhase), escHolding() ? " +HOLD" : "",
           (unsigned)sManualUs, (unsigned)gEscOutUs, (unsigned)escCycleNum(),
           (unsigned)sCycleTarget, sTest ? (sAborted ? " TEST(aborted)" : " TEST") : "");
  out.print(line);
}
