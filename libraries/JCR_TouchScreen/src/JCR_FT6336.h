/* ==========================================================================
 * JCR_FT6336.h — reliable FT6336U / FT6236 capacitive touch for RP2040.
 * MIT licensed.
 *
 * WHY THIS EXISTS
 * Driving an FT6336U the obvious way — wait for the INT pin, read the
 * registers from your main loop — drops taps. Two causes: the controller's
 * default trigger mode only pulses INT on state *changes*, so a short tap can
 * be missed entirely; and any slow frame in your UI stretches the interval
 * between reads until brief contacts land and lift unseen.
 *
 * This driver fixes both by inverting the relationship:
 *   - the controller runs in POLLING mode (0xA4 = 0x00) and INT is only
 *     counted as a diagnostic — nothing waits on it;
 *   - you call service() at a fixed, fast cadence (200 Hz by default) from
 *     wherever you like, and the driver samples on its own clock;
 *   - press/release transitions are queued as discrete events, so however
 *     slow your drawing gets, a tap is *registered* even if its handling is
 *     delayed. Queue overflow is counted, never silent.
 *
 * THE DRIVER NEVER CLAIMS A CORE. service() is the primitive; call it from
 * loop1(), from loop(), or interleaved inside your own sampling burst. On an
 * RP2040 the reliable arrangement is to service touch on core 1 while core 0
 * draws — see examples/DualCoreSampling for the pattern where core 1 also has
 * its own ADC work to do. JCR_TouchCore1 wraps the simple case.
 *
 * SAFETY: service() (producer) and popEvent()/getTouch() (consumer) may run on
 * different cores with no locking. The event queue is a single-producer /
 * single-consumer ring and the live sample is seqlock-protected; both are safe
 * on Cortex-M0+, which neither reorders nor caches. Do not call service() from
 * two cores at once.
 * ========================================================================*/
#pragma once

#include <Arduino.h>
#include <Wire.h>

enum JCRTouchEventType : uint8_t { JCR_TOUCH_NONE = 0, JCR_TOUCH_DOWN = 1, JCR_TOUCH_UP = 2 };

struct JCRTouchPoint {
  int16_t  x, y;          /* mapped to screen coordinates                 */
  uint16_t rawX, rawY;    /* controller's native frame                    */
  uint8_t  points;        /* contacts currently reported (0..2)           */
  bool     down;          /* debounced contact state                      */
  uint32_t downMicros;    /* micros() at the press that started it        */
};

struct JCRTouchEvent {
  JCRTouchEventType type;
  int16_t  x, y;
  uint16_t rawX, rawY;
  uint32_t atMicros;
};

/* Diagnostics — all cheap counters, useful on a debug screen. */
struct JCRTouchStats {
  uint32_t downs, ups;
  uint32_t i2cErrors;      /* transaction failed                          */
  uint32_t rangeGlitches;  /* coordinate outside the panel                */
  uint32_t jumpGlitches;   /* impossible movement within one sample       */
  uint32_t countGlitches;  /* contact count the part cannot report        */
  uint32_t dropouts;       /* single empty read bridged mid-contact       */
  uint32_t overflows;      /* event queue full — a press/release was lost */
  uint32_t intEdges;       /* INT pin activity (diagnostic only)          */
  uint32_t sampleHz;       /* achieved service() rate, refreshed each 1 s */
  /* Controller health, added for chasing responsiveness that fades with
   * time. regDrift counts every time the periodic read-back found one of the
   * configuration registers no longer holding what begin() wrote — the part
   * reverting to trigger or monitor mode on its own — and lastDriftReg says
   * which. serviceUsMax is the longest single sample transaction seen, so a
   * slow or stuck I2C bus is a number rather than a mystery. reinits counts
   * requestReinit() calls that were honoured. */
  uint32_t regDrift;
  uint8_t  lastDriftReg;
  uint32_t driftByReg[4];  /* per register: 0x00, 0x86, 0x88, 0xA4. All four
                            * climbing together = the part is resetting;
                            * one alone = something specific to that register */
  uint32_t serviceUsMax;
  uint32_t reinits;
};

class JCR_FT6336 {
 public:
  static const uint8_t kAddress = 0x38;

  /* rstPin/intPin may be -1. intPin is only ever used to count edges. */
  JCR_FT6336(TwoWire &wire, int8_t sdaPin, int8_t sclPin,
             int8_t rstPin = -1, int8_t intPin = -1);

  bool begin(uint32_t i2cHz = 400000, uint16_t sampleHz = 200);

  /* Map the controller's native frame onto your screen. nativeW/nativeH are
   * the touch panel's own portrait dimensions (320x480 for a 3.5" ST7796
   * module); rotation must match the display's. invertX/invertY are escape
   * hatches for modules wired differently — if the on-screen crosshair
   * mirrors your finger, flip one. */
  void setMapping(int16_t nativeW, int16_t nativeH, uint8_t rotation,
                  bool invertX = false, bool invertY = false);

  /* Sample if the next slot is due. Cheap to call as often as you like. */
  void service();
  /* Sample immediately, ignoring the cadence. Rarely needed. */
  void serviceNow();

  /* Latest debounced state — coherent snapshot, safe from the other core. */
  void getTouch(JCRTouchPoint &out) const;
  bool isDown() const;

  /* Drain queued press/release events. Returns false when empty. */
  bool popEvent(JCRTouchEvent &out);
  void flushEvents();

  void getStats(JCRTouchStats &out) const;
  /* Clear serviceUsMax only — the per-window "worst" for telemetry. Consumer
   * side, same request mechanism as resetStats(). */
  void resetServiceMax();
  /* Re-run begin() on the servicing core at its next sample: a bench test for
   * "has the controller lost its configuration". Safe from the other core. */
  void requestReinit();
  /* Safe to call from the consumer core: it only raises a request, which the
   * servicing core acts on at the top of its next sample. Keeps the
   * one-writer-per-direction rule that makes the counters lock-free. */
  void resetStats();

  bool    ok()         const { return _ok; }
  uint8_t chipId()     const { return _chipId; }
  uint8_t firmwareId() const { return _fwId; }
  uint8_t vendorId()   const { return _vendorId; }

  /* Tunables — defaults are the values validated on hardware. */
  void setReleaseConfirm(uint8_t samples) { _releaseConfirm = samples; }
  void setJumpThreshold(uint16_t px)      { _jumpPx = px; }

  /* Called from the INT ISR if you wire one up; counts edges only. */
  void noteInterrupt() { _stats.intEdges++; }

 private:
  static const uint8_t kQueueSize = 16;   /* power of two */

  bool writeReg(uint8_t reg, uint8_t val);
  bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n);
  void mapPoint(uint16_t rawX, uint16_t rawY, int16_t &x, int16_t &y) const;
  void pushEvent(JCRTouchEventType t, uint16_t rawX, uint16_t rawY);

  TwoWire *_wire;
  int8_t   _sda, _scl, _rst, _int;
  bool     _ok;
  uint8_t  _chipId, _fwId, _vendorId;

  /* mapping */
  int16_t _nativeW, _nativeH, _screenW, _screenH;
  uint8_t _rot;
  bool    _invX, _invY;

  /* cadence */
  uint32_t _periodUs, _nextUs;
  uint32_t _sampleCount, _hzWindowMs;

  /* state machine (producer side only) */
  bool     _down;
  uint8_t  _emptyRun, _releaseConfirm;
  uint16_t _jumpPx;
  uint16_t _lastRawX, _lastRawY;
  uint32_t _downMicros;

  /* seqlock-published live sample */
  volatile uint32_t _seq;
  volatile uint16_t _pubRawX, _pubRawY;
  volatile uint8_t  _pubPoints;
  volatile bool     _pubDown;
  volatile uint32_t _pubDownMicros;

  /* SPSC event ring */
  JCRTouchEvent      _queue[kQueueSize];
  volatile uint8_t   _qHead;   /* producer writes */
  volatile uint8_t   _qTail;   /* consumer writes */

  volatile JCRTouchStats _stats;
  volatile bool          _statsResetReq;
  volatile bool          _svcMaxResetReq;
  volatile bool          _reinitReq;

  /* register watchdog */
  uint32_t _i2cHz, _sampleHzCfg;
  uint32_t _lastVerifyMs;
  bool     _wireStarted;
  bool verifyRegs();
};

/* --------------------------------------------------------------------------
 * Convenience for sketches that have no other use for core 1: hands the whole
 * core to touch servicing. Do NOT use this if your own code needs core 1 —
 * call service() from your own loop1() instead (see DualCoreSampling).
 * ------------------------------------------------------------------------*/
namespace JCRTouchCore1 {
void attach(JCR_FT6336 &touch);   /* call from setup1() */
void run();                       /* call from loop1()  */
}
