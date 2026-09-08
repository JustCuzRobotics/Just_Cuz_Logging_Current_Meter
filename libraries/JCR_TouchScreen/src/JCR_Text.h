/* ==========================================================================
 * JCR_Text.h — bitmap font renderer for JCR_TFT. MIT licensed.
 *
 * One renderer handles both fixed-pitch and proportional fonts. A glyph is
 * stored column-major, bit 0 = top row, ceil(height/8) bytes per column —
 * the layout extras/make_fonts.py emits.
 *
 * A fixed-pitch font (offset/glyphW/advance all null) is packed as a flat
 * array: glyph g starts at (g - first) * fixedWidth * bytesPerCol. That keeps
 * a 5x7 font down to one 320-byte table with no side arrays.
 *
 * Text is drawn as opaque cells (foreground over background), which is what
 * makes fixed-width fields erasable by simply redrawing them.
 * ========================================================================*/
#pragma once

#include <Arduino.h>
#include "JCR_TFT.h"

struct JCRFont {
  const uint8_t  *data;
  const uint16_t *offset;       /* null => fixed-pitch layout              */
  const uint8_t  *glyphW;       /* null => every glyph is fixedWidth wide  */
  const uint8_t  *advance;      /* null => every glyph advances fixedAdv   */
  uint8_t         height;
  uint8_t         bytesPerCol;
  uint8_t         first, last;  /* inclusive ASCII range                   */
  uint8_t         fixedWidth;
  uint8_t         fixedAdv;
  bool            uppercaseOnly;/* map a-z to A-Z before lookup            */
};

class JCR_Text {
 public:
  explicit JCR_Text(JCR_TFT &tft) : _tft(&tft), _font(nullptr), _scale(1) {}

  void setFont(const JCRFont &f) { _font = &f; }
  void setScale(uint8_t s)       { _scale = s ? s : 1; }
  uint8_t scale() const          { return _scale; }
  const JCRFont *font() const    { return _font; }

  int16_t height() const   { return _font ? (int16_t)_font->height * _scale : 0; }
  int16_t textWidth(const char *s) const;
  /* Advance of one glyph — with a fixed-pitch (tabular) font this is the
   * per-character cell width, which is what cached numeric fields size to. */
  int16_t charWidth(char c) const;

  void draw(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg);
  void drawCentered(int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg);
  void drawRight(int16_t rightX, int16_t y, const char *s, uint16_t fg, uint16_t bg);

 private:
  int16_t glyphIndex(char c) const;   /* raw table index, -1 if out of range */
  int16_t resolve(char c) const;      /* index actually drawn, with fallback */

  JCR_TFT       *_tft;
  const JCRFont *_font;
  uint8_t        _scale;
};
