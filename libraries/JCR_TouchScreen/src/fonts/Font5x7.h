/* ==========================================================================
 * Font5x7.h — compact 5x7 fixed-pitch font, ASCII 32..95 (uppercase only).
 * MIT licensed: these glyphs were drawn from scratch for this library, so
 * unlike most bitmap fonts floating around they carry no license baggage.
 *
 * Fixed-pitch layout: 5 columns per glyph, 1 byte per column, bit 0 = top
 * row, 6 px advance (5 ink + 1 gap). Scales by integer factors via
 * JCR_Text::setScale().
 * ========================================================================*/
#pragma once
#include "../JCR_Text.h"

extern const JCRFont JCR_Font5x7;
