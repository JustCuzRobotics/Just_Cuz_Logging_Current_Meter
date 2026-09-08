/* ==========================================================================
 * JCR_ST7796.h — ST7796(S/U) controller for JCR_TFT. MIT licensed.
 *
 * Native panel is 320x480 portrait; rotations 1 and 3 give 480x320 landscape.
 * The init sequence below is the one validated on the lcdwiki MSP3525/MSP3526
 * 3.5" IPS module and matches Arduino_GFX's ST7796 table (same gamma, same
 * VCOM), so panels that worked there work here.
 *
 * ---- Adding another controller ----
 * Copy this file, keep the same three overrides, and change:
 *   writeInit()    your controller's command table
 *   nativeSize()   its rotation-0 portrait dimensions
 *   madctlFor()    its MADCTL byte per rotation (MY 0x80, MX 0x40, MV 0x20,
 *                  BGR 0x08 — most panels want BGR set)
 * If the visible area is inset in the controller's address space (many small
 * ST7789s), also override rotationOffset(). Nothing else needs to change:
 * every primitive already works off the dimensions those hooks report.
 * ========================================================================*/
#pragma once

#include "JCR_TFT.h"

class JCR_ST7796 : public JCR_TFT {
 public:
  JCR_ST7796(int8_t csPin, int8_t dcPin, int8_t rstPin = -1, int8_t blPin = -1)
      : JCR_TFT(csPin, dcPin, rstPin, blPin) {}

 protected:
  void nativeSize(int16_t &w, int16_t &h) override { w = 320; h = 480; }

  uint8_t madctlFor(uint8_t rotation) override {
    /* MX|BGR, MV|BGR, MY|BGR, MY|MX|MV|BGR */
    static const uint8_t kMadctl[4] = { 0x48, 0x28, 0x88, 0xE8 };
    return kMadctl[rotation & 3];
  }

  void writeInit() override {
    writeCommand(0x01); delay(120);                 /* SWRESET              */
    writeCommand(0x11); delay(120);                 /* SLPOUT               */

    cmd1(0xF0, 0xC3);                               /* command-set unlock   */
    cmd1(0xF0, 0x96);
    cmd1(0x3A, 0x55);                               /* 16-bit colour        */
    cmd1(0xB4, 0x01);                               /* 1-dot inversion      */

    static const uint8_t dfc[]  = { 0x80, 0x02, 0x3B };
    writeCommand(0xB6, dfc, sizeof dfc);            /* display function ctl */

    static const uint8_t doca[] = { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 };
    writeCommand(0xE8, doca, sizeof doca);

    cmd1(0xC1, 0x06);                               /* power control 2      */
    cmd1(0xC2, 0xA7);                               /* power control 3      */
    cmd1(0xC5, 0x18);                               /* VCOM                 */
    delay(120);

    static const uint8_t gammaP[] = { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
                                      0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B };
    writeCommand(0xE0, gammaP, sizeof gammaP);
    static const uint8_t gammaN[] = { 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
                                      0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B };
    writeCommand(0xE1, gammaN, sizeof gammaN);
    delay(120);

    cmd1(0xF0, 0x3C);                               /* command-set lock     */
    cmd1(0xF0, 0x69);
    delay(120);

    writeCommand(0x20);                             /* inversion off        */
    writeCommand(0x38);                             /* idle mode off        */
    writeCommand(0x29); delay(20);                  /* display on           */
  }

 private:
  void cmd1(uint8_t c, uint8_t a) { writeCommand(c, &a, 1); }
};
