/* JCR_TFT.cpp — see JCR_TFT.h. MIT licensed. */
#include "JCR_TFT.h"

JCR_TFT::JCR_TFT(int8_t csPin, int8_t dcPin, int8_t rstPin, int8_t blPin)
    : _spi(&SPI), _settings(40000000UL, MSBFIRST, SPI_MODE0),
      _cs(csPin), _dc(dcPin), _rst(rstPin), _bl(blPin),
      _sck(-1), _mosi(-1), _miso(-1),
      _w(0), _h(0), _nativeW(0), _nativeH(0), _colStart(0), _rowStart(0),
      _rot(0), _txn(0), _buf(nullptr), _bufLen(0) {}

JCR_TFT::~JCR_TFT() { free(_buf); }

void JCR_TFT::setSPIPins(int8_t sck, int8_t mosi, int8_t miso) {
  _sck = sck; _mosi = mosi; _miso = miso;
}

bool JCR_TFT::begin(uint32_t spiHz, uint8_t rotation) {
  _settings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);

  nativeSize(_nativeW, _nativeH);
  _bufLen = (_nativeW > _nativeH) ? _nativeW : _nativeH;
  free(_buf);
  _buf = (uint16_t *)malloc((size_t)_bufLen * sizeof(uint16_t));
  if (!_buf) { _bufLen = 0; return false; }

  pinMode(_cs, OUTPUT); digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT); digitalWrite(_dc, HIGH);
  if (_bl >= 0) { pinMode(_bl, OUTPUT); digitalWrite(_bl, LOW); }

  if (_sck  >= 0) _spi->setSCK(_sck);
  if (_mosi >= 0) _spi->setTX(_mosi);
  if (_miso >= 0) _spi->setRX(_miso);
  _spi->begin();

  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH); delay(10);
    digitalWrite(_rst, LOW);  delay(20);
    digitalWrite(_rst, HIGH); delay(120);
  }

  startWrite();
  writeInit();
  endWrite();

  setRotation(rotation);
  backlight(true);
  return true;
}

void JCR_TFT::setRotation(uint8_t r) {
  _rot = r & 3;
  if (_rot & 1) { _w = _nativeH; _h = _nativeW; }
  else          { _w = _nativeW; _h = _nativeH; }
  rotationOffset(_rot, _colStart, _rowStart);
  startWrite();
  uint8_t m = madctlFor(_rot);
  writeCommand(cmdMadctl(), &m, 1);
  endWrite();
}

void JCR_TFT::backlight(bool on) {
  /* Driven as a solid level, never PWM'd — see the README: on boards that
   * share an analog front end, PWM backlight injects measurable noise. */
  if (_bl >= 0) digitalWrite(_bl, on ? HIGH : LOW);
}

void JCR_TFT::invertDisplay(bool inv) {
  startWrite();
  writeCommand(inv ? cmdInvOn() : cmdInvOff());
  endWrite();
}

void JCR_TFT::sleep(bool enable) {
  startWrite();
  writeCommand(enable ? cmdSlpIn() : cmdSlpOut());
  endWrite();
  delay(120);
}

/* ---------------------------------------------------------------- bus ---- */
void JCR_TFT::startWrite() {
  if (_txn++ == 0) {
    _spi->beginTransaction(_settings);
    digitalWrite(_cs, LOW);
  }
}
void JCR_TFT::endWrite() {
  if (_txn && --_txn == 0) {
    digitalWrite(_cs, HIGH);
    _spi->endTransaction();
  }
}
void JCR_TFT::writeCommand(uint8_t c) {
  digitalWrite(_dc, LOW);
  _spi->transfer(c);
  digitalWrite(_dc, HIGH);
}
void JCR_TFT::writeCommand(uint8_t c, const uint8_t *data, size_t n) {
  writeCommand(c);
  if (n) _spi->transfer((const void *)data, nullptr, n);
}
void JCR_TFT::writeBytes(const uint8_t *data, size_t n) {
  if (n) _spi->transfer((const void *)data, nullptr, n);
}

void JCR_TFT::setAddrWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
  int16_t x0 = x + _colStart, x1 = x + w - 1 + _colStart;
  int16_t y0 = y + _rowStart, y1 = y + h - 1 + _rowStart;
  uint8_t d[4];
  d[0] = (uint8_t)(x0 >> 8); d[1] = (uint8_t)x0;
  d[2] = (uint8_t)(x1 >> 8); d[3] = (uint8_t)x1;
  writeCommand(cmdCaset(), d, 4);
  d[0] = (uint8_t)(y0 >> 8); d[1] = (uint8_t)y0;
  d[2] = (uint8_t)(y1 >> 8); d[3] = (uint8_t)y1;
  writeCommand(cmdRaset(), d, 4);
  writeCommand(cmdRamwr());
}

void JCR_TFT::pushPixelsBE(const uint16_t *src, size_t count) {
  if (count) _spi->transfer((const void *)src, nullptr, count * 2);
}

/* Fills the scratch buffer once, then repeats it — one SPI call per
 * buffer-full, independent of how large the run is. */
void JCR_TFT::pushColor(uint16_t color, uint32_t count) {
  if (!count || !_buf) return;
  uint16_t be = swap565(color);
  int16_t chunk = (count < (uint32_t)_bufLen) ? (int16_t)count : _bufLen;
  for (int16_t i = 0; i < chunk; i++) _buf[i] = be;
  while (count) {
    uint32_t n = (count < (uint32_t)chunk) ? count : (uint32_t)chunk;
    _spi->transfer((const void *)_buf, nullptr, n * 2);
    count -= n;
  }
}

/* ---------------------------------------------------------- primitives ---- */
void JCR_TFT::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _w) w = _w - x;
  if (y + h > _h) h = _h - y;
  if (w <= 0 || h <= 0) return;
  startWrite();
  setAddrWindow(x, y, w, h);
  pushColor(color, (uint32_t)w * (uint32_t)h);
  endWrite();
}

void JCR_TFT::fillScreen(uint16_t color) { fillRect(0, 0, _w, _h, color); }
void JCR_TFT::drawPixel(int16_t x, int16_t y, uint16_t color) { fillRect(x, y, 1, 1, color); }
void JCR_TFT::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { fillRect(x, y, w, 1, color); }
void JCR_TFT::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { fillRect(x, y, 1, h, color); }

void JCR_TFT::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  startWrite();
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
  endWrite();
}

void JCR_TFT::drawVRun(int16_t x, int16_t y, const uint16_t *colors, int16_t count) {
  if (!_buf || count <= 0) return;
  if (y < 0) { colors -= y; count += y; y = 0; }
  if (y + count > _h) count = _h - y;
  if (count <= 0 || x < 0 || x >= _w) return;
  if (count > _bufLen) count = _bufLen;
  for (int16_t i = 0; i < count; i++) _buf[i] = swap565(colors[i]);
  startWrite();
  setAddrWindow(x, y, 1, count);
  pushPixelsBE(_buf, (size_t)count);
  endWrite();
}

void JCR_TFT::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  /* Straight runs are far cheaper as a single windowed write. */
  if (x0 == x1) { drawFastVLine(x0, (y0 < y1) ? y0 : y1, abs(y1 - y0) + 1, color); return; }
  if (y0 == y1) { drawFastHLine((x0 < x1) ? x0 : x1, y0, abs(x1 - x0) + 1, color); return; }
  int16_t dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
  int32_t err = dx + dy;
  startWrite();                       /* one transaction for the whole line */
  for (;;) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
  endWrite();
}

/* Corner bits: 1 = NW arc, 2 = NE, 4 = SE, 8 = SW. */
void JCR_TFT::drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, uint16_t color) {
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    if (corners & 0x4) { drawPixel(x0 + x, y0 + y, color); drawPixel(x0 + y, y0 + x, color); }
    if (corners & 0x2) { drawPixel(x0 + x, y0 - y, color); drawPixel(x0 + y, y0 - x, color); }
    if (corners & 0x8) { drawPixel(x0 - y, y0 + x, color); drawPixel(x0 - x, y0 + y, color); }
    if (corners & 0x1) { drawPixel(x0 - y, y0 - x, color); drawPixel(x0 - x, y0 - y, color); }
  }
}

void JCR_TFT::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t sides,
                               int16_t delta, uint16_t color) {
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
  int16_t px = x, py = y;
  delta++;
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    if (x < y + 1) {
      if (sides & 1) drawFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
      if (sides & 2) drawFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
    }
    if (y != py) {
      if (sides & 1) drawFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
      if (sides & 2) drawFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
      py = y;
    }
    px = x;
  }
}

void JCR_TFT::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  startWrite();
  drawPixel(x0, y0 + r, color); drawPixel(x0, y0 - r, color);
  drawPixel(x0 + r, y0, color); drawPixel(x0 - r, y0, color);
  drawCircleHelper(x0, y0, r, 0xF, color);
  endWrite();
}

void JCR_TFT::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  startWrite();
  drawFastVLine(x0, y0 - r, 2 * r + 1, color);
  fillCircleHelper(x0, y0, r, 3, 0, color);
  endWrite();
}

void JCR_TFT::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  int16_t maxR = ((w < h) ? w : h) / 2;
  if (r > maxR) r = maxR;
  if (r < 0) r = 0;
  startWrite();
  drawFastHLine(x + r, y, w - 2 * r, color);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  drawFastVLine(x, y + r, h - 2 * r, color);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  drawCircleHelper(x + r,         y + r,         r, 0x1, color);
  drawCircleHelper(x + w - r - 1, y + r,         r, 0x2, color);
  drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 0x4, color);
  drawCircleHelper(x + r,         y + h - r - 1, r, 0x8, color);
  endWrite();
}

void JCR_TFT::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  int16_t maxR = ((w < h) ? w : h) / 2;
  if (r > maxR) r = maxR;
  if (r < 0) r = 0;
  startWrite();
  fillRect(x + r, y, w - 2 * r, h, color);
  fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
  fillCircleHelper(x + r,         y + r, r, 2, h - 2 * r - 1, color);
  endWrite();
}
