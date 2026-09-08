/* JCR_Text.cpp — see JCR_Text.h. MIT licensed. */
#include "JCR_Text.h"

int16_t JCR_Text::glyphIndex(char c) const {
  if (!_font) return -1;
  if (_font->uppercaseOnly && c >= 'a' && c <= 'z') c -= 32;
  uint8_t u = (uint8_t)c;
  if (u < _font->first || u > _font->last) return -1;
  return (int16_t)(u - _font->first);
}

/* A glyph inside the font's range but with a zero advance is one the
 * generator was not asked to include — treat it as unmapped, not as a
 * zero-width character, or strings silently lose both the glyph and its
 * spacing. */
int16_t JCR_Text::resolve(char c) const {
  if (!_font) return -1;
  int16_t g = glyphIndex(c);
  if (g >= 0) {
    uint8_t adv = _font->advance ? _font->advance[g] : _font->fixedAdv;
    if (adv) return g;
  }
  g = glyphIndex('-');                       /* fallback glyph */
  if (g >= 0) {
    uint8_t adv = _font->advance ? _font->advance[g] : _font->fixedAdv;
    if (adv) return g;
  }
  return -1;
}

int16_t JCR_Text::charWidth(char c) const {
  if (!_font) return 0;
  int16_t g = resolve(c);
  if (g < 0) return 0;
  uint8_t adv = _font->advance ? _font->advance[g] : _font->fixedAdv;
  return (int16_t)adv * _scale;
}

int16_t JCR_Text::textWidth(const char *s) const {
  int16_t w = 0;
  for (; *s; s++) w += charWidth(*s);
  return w;
}

void JCR_Text::draw(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
  if (!_font || !_tft) return;
  const int16_t sh = (int16_t)_font->height * _scale;
  uint16_t fgBE = JCR_TFT::swap565(fg);
  uint16_t bgBE = JCR_TFT::swap565(bg);
  uint16_t *buf = _tft->scratch();
  if (!buf) return;

  _tft->startWrite();
  for (; *s; s++) {
    int16_t g = resolve(*s);
    if (g < 0) continue;

    const uint8_t adv = _font->advance ? _font->advance[g] : _font->fixedAdv;
    const uint8_t gw  = _font->glyphW  ? _font->glyphW[g]  : _font->fixedWidth;
    const int16_t cellW = (int16_t)adv * _scale;

    /* Cells are drawn whole or not at all: a partially overhanging cell would
     * need a window the controller clamps, and the surplus pixels would wrap
     * back to the window origin as visible corruption. The pen still advances
     * so the rest of the string stays aligned. */
    if (x < 0 || y < 0 || x + cellW > _tft->width() || y + sh > _tft->height() ||
        cellW > _tft->scratchLen()) {
      x += cellW;
      continue;
    }

    const uint8_t *gd = _font->data +
        (_font->offset ? _font->offset[g]
                       : (uint16_t)g * _font->fixedWidth * _font->bytesPerCol);

    _tft->setAddrWindow(x, y, cellW, sh);
    for (uint8_t row = 0; row < _font->height; row++) {
      uint16_t *p = buf;
      for (uint8_t col = 0; col < adv; col++) {
        bool on = (col < gw) &&
                  ((gd[col * _font->bytesPerCol + (row >> 3)] >> (row & 7)) & 1);
        uint16_t v = on ? fgBE : bgBE;
        for (uint8_t sx = 0; sx < _scale; sx++) *p++ = v;
      }
      for (uint8_t sy = 0; sy < _scale; sy++)
        _tft->pushPixelsBE(buf, (size_t)cellW);
    }
    x += cellW;
  }
  _tft->endWrite();
}

void JCR_Text::drawCentered(int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
  draw(cx - textWidth(s) / 2, y, s, fg, bg);
}

void JCR_Text::drawRight(int16_t rightX, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
  draw(rightX - textWidth(s), y, s, fg, bg);
}
