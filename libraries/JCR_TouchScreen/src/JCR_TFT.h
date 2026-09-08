/* ==========================================================================
 * JCR_TFT.h — resolution-agnostic SPI TFT base class.
 *
 * Part of JCR_TouchScreen. MIT licensed — see LICENSE.
 *
 * This class owns every drawing primitive and knows nothing about which
 * controller it is talking to. A controller subclass supplies three things:
 *   writeInit()      — the panel's power-on command sequence
 *   nativeSize()     — the panel's NATIVE (rotation-0, portrait) dimensions
 *   madctlFor(rot)   — the MADCTL byte for each of the four rotations
 * and optionally rotationOffset() for panels whose visible area is inset
 * inside the controller's larger address space (common on small ST7789s).
 *
 * Nothing here is fixed at compile time: width/height are instance state
 * derived from the native size and the current rotation, the scanline
 * buffer is allocated at begin() to the panel's longest edge, and every
 * primitive clips against the live dimensions. Supporting a different
 * resolution — or a different controller at a different resolution — means
 * writing a subclass, not editing this file.
 *
 * RP2040 / arduino-pico only (uses SPIClassRP2040's pin setters).
 * ========================================================================*/
#pragma once

#include <Arduino.h>
#include <SPI.h>

#if !defined(ARDUINO_ARCH_RP2040)
#error "JCR_TouchScreen targets the RP2040 (arduino-pico core by earlephilhower)."
#endif

/* A rectangle, with hit-testing. Defined here rather than in your sketch on
 * purpose: the Arduino IDE inserts auto-generated prototypes ABOVE anything
 * you declare in a .ino, so a struct defined in a sketch and used as a
 * function parameter fails to compile ("does not name a type"). Types that
 * live in a header dodge that entirely. */
struct JCRRect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  /* Grown by `slop` on every side — handy for making a touch target more
   * forgiving than the box you drew. */
  bool containsSlop(int16_t px, int16_t py, int16_t slop) const {
    return px >= x - slop && px < x + w + slop &&
           py >= y - slop && py < y + h + slop;
  }
  int16_t cx() const { return x + w / 2; }
  int16_t cy() const { return y + h / 2; }
};

class JCR_TFT {
 public:
  /* rstPin / blPin may be -1 if the panel ties them off in hardware. */
  JCR_TFT(int8_t csPin, int8_t dcPin, int8_t rstPin = -1, int8_t blPin = -1);
  virtual ~JCR_TFT();

  /* Optional: route SPI to specific pins before begin(). Pass -1 to leave a
   * pin at the core's default. MISO is unused for drawing but must be parked
   * sensibly on modules that share it (see the README's SD_CS note). */
  void setSPIPins(int8_t sck, int8_t mosi, int8_t miso = -1);
  void setSPI(SPIClassRP2040 &spi) { _spi = &spi; }

  /* Allocates the scanline buffer, resets and initialises the panel, applies
   * the rotation, and switches the backlight on. Returns false only if the
   * scanline buffer could not be allocated. */
  bool begin(uint32_t spiHz = 40000000UL, uint8_t rotation = 0);

  void    setRotation(uint8_t r);
  uint8_t rotation() const { return _rot; }
  int16_t width()    const { return _w; }
  int16_t height()   const { return _h; }

  void backlight(bool on);
  void invertDisplay(bool inv);
  void sleep(bool enable);

  /* ---- primitives (all clip to the current width()/height()) ---- */
  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

  /* Vertical run of arbitrary colours in one windowed write — the fast path
   * for plotting a column of a graph, or erasing one over a grid. */
  void drawVRun(int16_t x, int16_t y, const uint16_t *colors, int16_t count);

  /* ---- low level, for custom blitters (JCR_Text uses these) ---- */
  void startWrite();                  /* nestable — only the outermost pair
                                       * touches CS / SPI transactions      */
  void endWrite();
  void setAddrWindow(int16_t x, int16_t y, int16_t w, int16_t h);
  void pushPixelsBE(const uint16_t *bigEndianSrc, size_t count);
  void pushColor(uint16_t color, uint32_t count);

  /* Scratch buffer, longest-edge sized. Shared; only valid between a
   * startWrite()/endWrite() pair you own. */
  uint16_t *scratch()          { return _buf; }
  int16_t   scratchLen() const { return _bufLen; }

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
  static uint16_t swap565(uint16_t c) { return __builtin_bswap16(c); }

 protected:
  /* ---- the three hooks a controller subclass must supply ---- */
  virtual void    writeInit() = 0;
  virtual void    nativeSize(int16_t &w, int16_t &h) = 0;
  virtual uint8_t madctlFor(uint8_t rotation) = 0;
  /* Panels whose visible window is inset in the controller's address space
   * override this; the default (0,0) suits full-size panels like the ST7796. */
  virtual void rotationOffset(uint8_t rotation, int16_t &colStart, int16_t &rowStart) {
    (void)rotation; colStart = 0; rowStart = 0;
  }
  /* Command opcodes — overridable for controllers that renumber them. */
  virtual uint8_t cmdCaset()  { return 0x2A; }
  virtual uint8_t cmdRaset()  { return 0x2B; }
  virtual uint8_t cmdRamwr()  { return 0x2C; }
  virtual uint8_t cmdMadctl() { return 0x36; }
  virtual uint8_t cmdInvOn()  { return 0x21; }
  virtual uint8_t cmdInvOff() { return 0x20; }
  virtual uint8_t cmdSlpIn()  { return 0x10; }
  virtual uint8_t cmdSlpOut() { return 0x11; }

  /* ---- helpers available to subclass init code (call inside startWrite) ---- */
  void writeCommand(uint8_t c);
  void writeCommand(uint8_t c, const uint8_t *data, size_t n);
  void writeBytes(const uint8_t *data, size_t n);

  void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, uint16_t color);
  void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t sides,
                        int16_t delta, uint16_t color);

  SPIClassRP2040 *_spi;
  SPISettings     _settings;
  int8_t  _cs, _dc, _rst, _bl;
  int8_t  _sck, _mosi, _miso;
  int16_t _w, _h;                 /* current, after rotation */
  int16_t _nativeW, _nativeH;     /* rotation-0 portrait */
  int16_t _colStart, _rowStart;
  uint8_t _rot;
  uint8_t _txn;                   /* startWrite nesting depth */
  uint16_t *_buf;
  int16_t   _bufLen;
};
