/* ==========================================================================
 * EscProfile.h — the cycle profile maths, with no Arduino dependency.
 *
 * Kept free of millis(), Servo and gSet on purpose: everything here is a pure
 * function of (phase, time into phase, profile), so it is unit-tested on the
 * host and the state machine in EscOut.cpp only has to decide WHICH phase it
 * is in and for how long.
 *
 * One cycle is RAMP_UP -> DWELL_HI -> RAMP_DN -> DWELL_LO. Ramps are linear in
 * microseconds; a 0 ms ramp is an instant step. PRE and POST sit at idle.
 *
 * PRE is the pre-roll every run starts with — cycles as well as Log Tests.
 * An ESC will not spin until it has seen a few seconds of idle after the
 * signal appears (beeps, or startup music on some firmware), so a run that
 * began the moment the output came up would eat the start of its own first
 * ramp and jerk. Its length is the operator's setting, since how long an ESC
 * takes to come up is a property of the ESC.
 *
 * Bidirectional ESCs: the low end is neutral (1500 us) and `reverse` mirrors
 * the high pulse about neutral, so a 1750 us forward profile runs 1250 us in
 * reverse. Unidirectional profiles ignore `reverse`.
 * ========================================================================*/
#pragma once
#include <stdint.h>

#define ESC_UNI_IDLE_US    1000
#define ESC_BIDI_IDLE_US   1500
#define ESC_ABS_MIN_US     1000
#define ESC_ABS_MAX_US     2000

#define ESC_TEST_POST_MS   5000     /* Log Test post-roll at idle         */

enum EscType : uint8_t { ESC_TYPE_UNI = 0, ESC_TYPE_BIDI = 1 };
enum EscDir  : uint8_t { ESC_DIR_FWD = 0, ESC_DIR_REV = 1, ESC_DIR_ALT = 2, ESC_DIR_COUNT = 3 };

enum EscPhase : uint8_t {
  PH_OFF = 0,     /* disarmed, nothing running                               */
  PH_MANUAL,      /* armed, manual set point                                 */
  PH_PRE,         /* pre-roll at idle, before cycle 1 of any run             */
  PH_RAMP_UP, PH_DWELL_HI, PH_RAMP_DN, PH_DWELL_LO,
  PH_POST,        /* Log Test post-roll (output idle, or off after a STOP)   */
  PH_COUNT
};

/* Field ORDER is part of the interface: the host tests build profiles with
 * aggregate initialisers, so a reorder would silently shuffle their values.
 * The dwells are 32-bit because they run to 3 minutes; the padding that costs
 * inside the struct is irrelevant (it is never serialised). */
struct EscProfile {
  uint16_t lowUs, highUs;           /* lowUs ignored for BIDI (neutral)     */
  uint16_t rampUpMs;                /* 0-3000 ms                            */
  uint32_t dwellHiMs;               /* 0.5 s - 3 min                        */
  uint16_t rampDnMs;
  uint32_t dwellLoMs;
  uint16_t preMs;                   /* pre-roll at idle before cycle 1      */
};

/* Idle / neutral pulse for an ESC type. */
static inline uint16_t escTypeIdleUs(uint8_t type) {
  return type == ESC_TYPE_BIDI ? ESC_BIDI_IDLE_US : ESC_UNI_IDLE_US;
}

/* The low end of the profile: the configured low for UNI, neutral for BIDI. */
static inline uint16_t profileLowUs(const EscProfile &p, uint8_t type) {
  return type == ESC_TYPE_BIDI ? ESC_BIDI_IDLE_US : p.lowUs;
}

/* The high end, mirrored about neutral for a reverse BIDI cycle. */
static inline uint16_t profileHighUs(const EscProfile &p, uint8_t type, bool reverse) {
  if (type == ESC_TYPE_BIDI && reverse)
    return (uint16_t)(2 * ESC_BIDI_IDLE_US - p.highUs);
  return p.highUs;
}

/* How long a phase lasts, in ms. 0 = move on immediately (0 ms ramp).
 * PH_OFF / PH_MANUAL are not timed here. */
static inline uint32_t profilePhaseMs(uint8_t ph, const EscProfile &p) {
  switch (ph) {
    case PH_PRE:      return p.preMs;
    case PH_RAMP_UP:  return p.rampUpMs;
    case PH_DWELL_HI: return p.dwellHiMs;
    case PH_RAMP_DN:  return p.rampDnMs;
    case PH_DWELL_LO: return p.dwellLoMs;
    case PH_POST:     return ESC_TEST_POST_MS;
    default:          return 0;
  }
}

/* Pulse for a profile phase `elapsedMs` into it. Ramps interpolate with
 * rounding and clamp at their end, so an overshooting tick never leaves the
 * [low, high] band. */
static inline uint16_t profilePulse(uint8_t ph, uint32_t elapsedMs, const EscProfile &p,
                                    uint8_t type, bool reverse) {
  int32_t lo = profileLowUs(p, type);
  int32_t hi = profileHighUs(p, type, reverse);
  switch (ph) {
    case PH_RAMP_UP:
    case PH_RAMP_DN: {
      uint32_t len = ph == PH_RAMP_UP ? p.rampUpMs : p.rampDnMs;
      int32_t from = ph == PH_RAMP_UP ? lo : hi;
      int32_t to   = ph == PH_RAMP_UP ? hi : lo;
      if (len == 0 || elapsedMs >= len) return (uint16_t)to;
      int32_t num = (to - from) * (int32_t)elapsedMs;
      int32_t d = (num >= 0 ? num + (int32_t)len / 2 : num - (int32_t)len / 2) / (int32_t)len;
      return (uint16_t)(from + d);
    }
    case PH_DWELL_HI: return (uint16_t)hi;
    case PH_DWELL_LO: return (uint16_t)lo;
    default:          return escTypeIdleUs(type);   /* PRE, POST, MANUAL idle   */
  }
}

/* Whether cycle `index` (0-based) runs in reverse for a direction setting. */
static inline bool profileCycleReverse(uint8_t dir, uint16_t index) {
  if (dir == ESC_DIR_REV) return true;
  if (dir == ESC_DIR_ALT) return (index & 1) != 0;
  return false;
}
