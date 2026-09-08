/* JCR_FT6336.cpp — see JCR_FT6336.h. MIT licensed. */
#include "JCR_FT6336.h"

/* Register map (FT6336U / FT6236) */
#define FT_REG_DEV_MODE   0x00
#define FT_REG_TD_STATUS  0x02   /* burst base: status, then 4 bytes/point */
#define FT_REG_CHIP_ID    0xA3
#define FT_REG_P_ACTIVE   0xA5
#define FT_REG_FW_ID      0xA6
#define FT_REG_VENDOR_ID  0xA8
#define FT_REG_G_MODE     0xA4   /* 0 = polling, 1 = trigger              */
#define FT_REG_MONITOR    0x86   /* 0 = forbid monitor (idle) mode        */
#define FT_REG_PERIOD_MON 0x88   /* scan period                           */

JCR_FT6336::JCR_FT6336(TwoWire &wire, int8_t sdaPin, int8_t sclPin,
                       int8_t rstPin, int8_t intPin)
    : _wire(&wire), _sda(sdaPin), _scl(sclPin), _rst(rstPin), _int(intPin),
      _ok(false), _chipId(0), _fwId(0), _vendorId(0),
      _nativeW(320), _nativeH(480), _screenW(320), _screenH(480),
      _rot(0), _invX(false), _invY(false),
      _periodUs(5000), _nextUs(0), _sampleCount(0), _hzWindowMs(0),
      _down(false), _emptyRun(0), _releaseConfirm(2), _jumpPx(60),
      _lastRawX(0), _lastRawY(0), _downMicros(0),
      _seq(0), _pubRawX(0), _pubRawY(0), _pubPoints(0), _pubDown(false),
      _pubDownMicros(0), _qHead(0), _qTail(0), _statsResetReq(false),
      _svcMaxResetReq(false), _reinitReq(false),
      _i2cHz(400000), _sampleHzCfg(200), _lastVerifyMs(0), _wireStarted(false) {
  memset((void *)&_stats, 0, sizeof(_stats));
}

bool JCR_FT6336::begin(uint32_t i2cHz, uint16_t sampleHz) {
  if (sampleHz == 0) sampleHz = 1;
  _periodUs = 1000000UL / sampleHz;
  _i2cHz = i2cHz;
  _sampleHzCfg = sampleHz;

  if (_int >= 0) pinMode(_int, INPUT_PULLUP);
  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH); delay(10);
    digitalWrite(_rst, LOW);  delay(20);
    digitalWrite(_rst, HIGH); delay(300);   /* the part is slow to boot */
  }

  /* Bus bring-up happens once. begin() can be re-run to restart the
   * controller (requestReinit), and the RP2040 core panics if a pin is
   * reassigned on a running bus. */
  if (!_wireStarted) {
    _wire->setSDA(_sda);
    _wire->setSCL(_scl);
    _wire->begin();
    _wireStarted = true;
  }
  _wire->setClock(100000);        /* configure at a conservative rate */
  delay(50);

  bool ok = true;
  ok &= writeReg(FT_REG_DEV_MODE, 0x00);    /* normal operating mode        */
  ok &= writeReg(FT_REG_P_ACTIVE, 0x00);    /* stay in active power state   */
  /* Forbid monitor mode. This one matters: the factory default lets the part
   * drop into a slow idle scan after ~30 s without contact, which reads as
   * "touch goes dead if you leave it alone for a bit". */
  ok &= writeReg(FT_REG_MONITOR, 0x00);
  ok &= writeReg(FT_REG_PERIOD_MON, 0x04);  /* fastest scan period          */
  /* Polling mode: we sample on our own clock and never wait on INT. */
  ok &= writeReg(FT_REG_G_MODE, 0x00);

  uint8_t v;
  if (readRegs(FT_REG_CHIP_ID, &v, 1))   _chipId   = v; else ok = false;
  if (readRegs(FT_REG_FW_ID, &v, 1))     _fwId     = v;
  if (readRegs(FT_REG_VENDOR_ID, &v, 1)) _vendorId = v;

  _wire->setClock(i2cHz);
  delay(5);

  _ok = ok;
  _nextUs = micros();
  _lastVerifyMs = millis();
  return ok;
}

/* The four registers begin() configures, and the values it wrote. Read back
 * periodically from the servicing core; any mismatch is counted, attributed,
 * and corrected. This exists because "touch fades after a couple of minutes"
 * has exactly the shape of the part quietly falling back into monitor or
 * trigger mode, and a counter that stays at zero is as useful as one that
 * climbs — it rules the controller out. */
bool JCR_FT6336::verifyRegs() {
  static const uint8_t kReg[4] = { FT_REG_DEV_MODE, FT_REG_MONITOR,
                                   FT_REG_PERIOD_MON, FT_REG_G_MODE };
  static const uint8_t kVal[4] = { 0x00, 0x00, 0x04, 0x00 };
  bool clean = true;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t v;
    if (!readRegs(kReg[i], &v, 1)) { _stats.i2cErrors++; return false; }
    if (v != kVal[i]) {
      clean = false;
      _stats.regDrift++;
      _stats.driftByReg[i]++;
      _stats.lastDriftReg = kReg[i];
      writeReg(kReg[i], kVal[i]);
    }
  }
  return clean;
}

void JCR_FT6336::requestReinit()   { _reinitReq = true; }
void JCR_FT6336::resetServiceMax() { _svcMaxResetReq = true; }

void JCR_FT6336::setMapping(int16_t nativeW, int16_t nativeH, uint8_t rotation,
                            bool invertX, bool invertY) {
  _nativeW = nativeW;
  _nativeH = nativeH;
  _rot     = rotation & 3;
  _invX    = invertX;
  _invY    = invertY;
  if (_rot & 1) { _screenW = nativeH; _screenH = nativeW; }
  else          { _screenW = nativeW; _screenH = nativeH; }
}

bool JCR_FT6336::writeReg(uint8_t reg, uint8_t val) {
  _wire->beginTransmission(kAddress);
  _wire->write(reg);
  _wire->write(val);
  return _wire->endTransmission() == 0;
}

bool JCR_FT6336::readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  _wire->beginTransmission(kAddress);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;   /* repeated start */
  if (_wire->requestFrom((uint8_t)kAddress, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)_wire->read();
  return true;
}

void JCR_FT6336::mapPoint(uint16_t rawX, uint16_t rawY, int16_t &x, int16_t &y) const {
  switch (_rot) {
    case 0:  x = (int16_t)rawX;                     y = (int16_t)rawY;                     break;
    case 1:  x = (int16_t)rawY;                     y = (int16_t)(_nativeW - 1 - rawX);     break;
    case 2:  x = (int16_t)(_nativeW - 1 - rawX);    y = (int16_t)(_nativeH - 1 - rawY);     break;
    default: x = (int16_t)(_nativeH - 1 - rawY);    y = (int16_t)rawX;                      break;
  }
  if (_invX) x = _screenW - 1 - x;
  if (_invY) y = _screenH - 1 - y;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= _screenW) x = _screenW - 1;
  if (y >= _screenH) y = _screenH - 1;
}

void JCR_FT6336::pushEvent(JCRTouchEventType t, uint16_t rawX, uint16_t rawY) {
  uint8_t head = _qHead;
  uint8_t next = (uint8_t)((head + 1) & (kQueueSize - 1));
  if (next == _qTail) { _stats.overflows++; return; }   /* full — count it */
  JCRTouchEvent &e = _queue[head];
  e.type = t;
  e.rawX = rawX;
  e.rawY = rawY;
  mapPoint(rawX, rawY, e.x, e.y);
  e.atMicros = micros();
  __sync_synchronize();          /* publish the payload before the index */
  _qHead = next;
}

bool JCR_FT6336::popEvent(JCRTouchEvent &out) {
  uint8_t tail = _qTail;
  if (tail == _qHead) return false;
  __sync_synchronize();
  out = _queue[tail];
  _qTail = (uint8_t)((tail + 1) & (kQueueSize - 1));
  return true;
}

void JCR_FT6336::flushEvents() { _qTail = _qHead; }

void JCR_FT6336::service() {
  uint32_t now = micros();
  if (_nextUs == 0) _nextUs = now;
  if ((int32_t)(now - _nextUs) < 0) return;          /* slot not due yet */
  _nextUs += _periodUs;
  if ((int32_t)(micros() - _nextUs) > 0) _nextUs = micros();  /* fell behind */
  serviceNow();
}

void JCR_FT6336::serviceNow() {
  if (_statsResetReq) {                 /* honoured on the producer core */
    uint32_t keepHz = _stats.sampleHz;
    memset((void *)&_stats, 0, sizeof(_stats));
    _stats.sampleHz = keepHz;
    _statsResetReq = false;
  }
  if (_svcMaxResetReq) { _stats.serviceUsMax = 0; _svcMaxResetReq = false; }
  if (_reinitReq) {
    _reinitReq = false;
    /* begin() blocks for ~400 ms (the part's reset). Acceptable: this is a
     * bench command, and it runs on the servicing core so the UI core is
     * untouched. Counters survive; only the controller is restarted. */
    uint32_t keep[2] = { _stats.reinits, _stats.regDrift };
    uint8_t  keepReg = _stats.lastDriftReg;
    bool wasDown = _down;
    begin(_i2cHz, (uint16_t)_sampleHzCfg);
    _stats.reinits = keep[0] + 1;
    _stats.regDrift = keep[1];
    _stats.lastDriftReg = keepReg;
    if (wasDown) { _down = false; _pubDown = false; pushEvent(JCR_TOUCH_UP, _lastRawX, _lastRawY); }
    return;
  }

  /* Register watchdog, every 500 ms, between samples. Bench data showed the
   * mode register losing its value ~every 50 s on average; half a second is
   * the longest the part is allowed to run misconfigured. */
  uint32_t nowMsW = millis();
  if (nowMsW - _lastVerifyMs >= 500) {
    _lastVerifyMs = nowMsW;
    verifyRegs();
  }

  uint8_t b[5];
  uint32_t t0 = micros();
  bool ok = readRegs(FT_REG_TD_STATUS, b, 5);
  uint32_t dt = micros() - t0;
  if (dt > _stats.serviceUsMax) _stats.serviceUsMax = dt;

  _sampleCount++;
  uint32_t nowMs = millis();
  if (nowMs - _hzWindowMs >= 1000) {
    _stats.sampleHz = _sampleCount;
    _sampleCount = 0;
    _hzWindowMs = nowMs;
  }

  if (!ok) { _stats.i2cErrors++; return; }

  uint8_t points = (uint8_t)(b[0] & 0x0F);
  if (points > 2) { _stats.countGlitches++; return; }

  if (points == 0) {
    /* Require several consecutive empty reads before believing a release:
     * a held finger occasionally reads as no-contact for a sample. */
    if (_down) {
      if (++_emptyRun >= _releaseConfirm) {
        _down = false;
        _emptyRun = 0;
        _stats.ups++;
        pushEvent(JCR_TOUCH_UP, _lastRawX, _lastRawY);
      }
    } else {
      _emptyRun = 0;
    }
  } else {
    uint16_t x = (uint16_t)(((uint16_t)(b[1] & 0x0F) << 8) | b[2]);
    uint16_t y = (uint16_t)(((uint16_t)(b[3] & 0x0F) << 8) | b[4]);

    if (x >= (uint16_t)_nativeW || y >= (uint16_t)_nativeH) {
      _stats.rangeGlitches++;
      return;
    }
    if (_down && _emptyRun) _stats.dropouts++;   /* bridged a dropout */
    _emptyRun = 0;

    if (_down) {
      int32_t dx = (int32_t)x - (int32_t)_lastRawX;
      int32_t dy = (int32_t)y - (int32_t)_lastRawY;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      if (dx > _jumpPx || dy > _jumpPx) _stats.jumpGlitches++;
    } else {
      _down = true;
      _downMicros = micros();
      _stats.downs++;
      pushEvent(JCR_TOUCH_DOWN, x, y);
    }
    _lastRawX = x;
    _lastRawY = y;
  }

  /* Publish the live sample (seqlock: odd sequence = write in progress). */
  _seq++;
  __sync_synchronize();
  _pubRawX       = _lastRawX;
  _pubRawY       = _lastRawY;
  _pubPoints     = points;
  _pubDown       = _down;
  _pubDownMicros = _downMicros;
  __sync_synchronize();
  _seq++;
}

void JCR_FT6336::getTouch(JCRTouchPoint &out) const {
  uint32_t s1, s2;
  do {
    s1 = _seq;
    __sync_synchronize();
    out.rawX       = _pubRawX;
    out.rawY       = _pubRawY;
    out.points     = _pubPoints;
    out.down       = _pubDown;
    out.downMicros = _pubDownMicros;
    __sync_synchronize();
    s2 = _seq;
  } while (s1 != s2 || (s1 & 1));
  mapPoint(out.rawX, out.rawY, out.x, out.y);
}

bool JCR_FT6336::isDown() const { return _pubDown; }

void JCR_FT6336::getStats(JCRTouchStats &out) const {
  out.downs         = _stats.downs;
  out.ups           = _stats.ups;
  out.i2cErrors     = _stats.i2cErrors;
  out.rangeGlitches = _stats.rangeGlitches;
  out.jumpGlitches  = _stats.jumpGlitches;
  out.countGlitches = _stats.countGlitches;
  out.dropouts      = _stats.dropouts;
  out.overflows     = _stats.overflows;
  out.intEdges      = _stats.intEdges;
  out.sampleHz      = _stats.sampleHz;
  out.regDrift      = _stats.regDrift;
  out.lastDriftReg  = _stats.lastDriftReg;
  for (uint8_t i = 0; i < 4; i++) out.driftByReg[i] = _stats.driftByReg[i];
  out.serviceUsMax  = _stats.serviceUsMax;
  out.reinits       = _stats.reinits;
}

void JCR_FT6336::resetStats() { _statsResetReq = true; }

/* ---------------------------------------------------------------------- */
namespace JCRTouchCore1 {
static JCR_FT6336 *s_touch = nullptr;
void attach(JCR_FT6336 &touch) { s_touch = &touch; }
void run() { if (s_touch) s_touch->service(); }
}
