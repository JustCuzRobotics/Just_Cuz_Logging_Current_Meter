#include "Widgets.h"
#include "EscOut.h"
#include <string.h>
#include "Screens.h"
#include "Logger.h"

void t5(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale) {
  gfxText.setFont(JCR_Font5x7);
  gfxText.setScale(scale);
  gfxText.draw(x, y, s, fg, bg);
}
int16_t t5Width(const char *s, uint8_t scale) { return (int16_t)strlen(s) * 6 * scale; }

/* Advance of one RUSSOBIG cell. The font is monospaced, so this is the
 * column width every numeric field sizes itself to. */
int16_t bigCellW() {
  /* Read straight from the font table: measuring via gfxText would leave the
   * shared renderer pointed at RUSSOBIG as a side effect. */
  return RUSSOBIG.advance ? RUSSOBIG.advance['0' - 32] : RUSSOBIG.fixedAdv;
}

void tRusso(const JCRFont &f, int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
  gfxText.setFont(f);
  gfxText.setScale(1);
  gfxText.draw(x, y, s, fg, bg);
}
void tRussoCentered(const JCRFont &f, int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg) {
  gfxText.setFont(f);
  gfxText.setScale(1);
  gfxText.drawCentered(cx, y, s, fg, bg);
}

/* The 5x7 font is fixed pitch, so a field's erase width is exactly
 * maxChars cells — no need to measure the string being replaced. */
bool field5(int16_t x, int16_t y, uint8_t maxChars, uint8_t scale,
            uint16_t fg, uint16_t bg, const char *text, char *cache) {
  if (cache && strncmp(cache, text, maxChars) == 0) return false;
  if (cache) { strncpy(cache, text, maxChars); cache[maxChars] = 0; }
  tft.fillRect(x, y, (int16_t)maxChars * 6 * scale, 8 * scale, bg);
  t5(x, y, text, fg, bg, scale);
  return true;
}

/* RUSSOBIG is monospaced for exactly this reason: a numeric value that
 * updates in place always occupies maxChars identical cells, so the erase
 * region is exact and the field never shifts. The hard truncate means a
 * too-long string can never paint past that region into a neighbour. */
bool fieldBig(int16_t x, int16_t y, uint8_t maxChars,
              uint16_t fg, uint16_t bg, const char *text, char *cache) {
  char t[12];
  if (maxChars > 11) maxChars = 11;
  strncpy(t, text, maxChars);
  t[maxChars] = 0;
  if (cache && strcmp(cache, t) == 0) return false;
  if (cache) strcpy(cache, t);
  tft.fillRect(x, y, (int16_t)maxChars * bigCellW(), RUSSOBIG.height, bg);
  tRusso(RUSSOBIG, x, y, t, fg, bg);
  return true;
}

void drawBackBtn(const JCRRect &r, bool pressed) {
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  t5(r.x + 10, r.y + 11, "< HOME", COL_TEXT_HI, fill);
}
void drawBackBtnCompact(const JCRRect &r, bool pressed) {
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  t5(r.x + r.w / 2 - 3, r.y + r.h / 2 - 4, "<", COL_TEXT_HI, fill);
}
void drawIconBtn(const JCRRect &r, bool pressed, char glyph) {
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  char s[2] = { glyph, 0 };
  t5(r.x + r.w / 2 - 3, r.y + r.h / 2 - 4, s, COL_TEXT_HI, fill);
}
void drawActionBtn(const JCRRect &r, bool pressed, const char *label) {
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 7, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 7, COL_BOX_BORDER);
  tRussoCentered(RUSSO22, r.cx(), r.y + (r.h - RUSSO22.height) / 2 + 1, label, COL_TEXT_HI, fill);
}

/* On/off must read at a glance: coloured when on, greyed when off. The
 * earlier near-identical slate fills were too subtle to see, which made a
 * working toggle look broken. */
void drawChip(const JCRRect &r, char letter, uint16_t color, bool on, int16_t textInset) {
  uint16_t fill   = on ? COL_BOX_PRESSED : COL_BG;
  uint16_t accent = on ? color : COL_DISABLED_TEXT;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, accent);
  char s[2] = { letter, 0 };
  t5(r.x + textInset, r.y + (r.h - 8) / 2, s, accent, fill);
}

void drawMiniStatChrome(const JCRRect &r, const char *label) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  t5(r.x + 6, r.y + 4, label, COL_TEXT_HI, COL_BOX_FILL);
}
void drawMiniStatValue(const JCRRect &r, uint16_t color, const char *text,
                       uint8_t textSize, char *cache) {
  int16_t y = (textSize <= 1) ? (r.y + 15) : (r.y + 18);
  field5(r.x + 6, y, 16, textSize, color, COL_BOX_FILL, text, cache);
}

void drawBigValueChrome(const JCRRect &r) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
}
bool drawBigValueField(const JCRRect &r, uint16_t valColor, const char *valText,
                       const char *unitText, char *cache) {
  if (cache && strncmp(cache, valText, 12) == 0) return false;
  if (cache) { strncpy(cache, valText, 12); cache[12] = 0; }
  tft.fillRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, COL_BOX_FILL);
  int16_t vy = r.y + (r.h - RUSSOBIG.height) / 2;
  tRusso(RUSSOBIG, r.x + 8, vy, valText, valColor, COL_BOX_FILL);
  int16_t vw = (int16_t)strlen(valText) * bigCellW();
  t5(r.x + 8 + vw + 6, vy + RUSSOBIG.height - 10, unitText, COL_TEXT_HI, COL_BOX_FILL);
  return true;
}

/* ------------------------------------------------------------------ toast --
 * There is no framebuffer to save and restore the pixels underneath, so on
 * expiry the screen's chrome is simply repainted. That is why every
 * paintXOnce() must stay idempotent and cheap. */
static char     gToastMsg[40];
static uint32_t gToastUntil = 0;

void showToast(const char *msg) {
  strncpy(gToastMsg, msg, sizeof(gToastMsg) - 1);
  gToastMsg[sizeof(gToastMsg) - 1] = 0;
  gToastUntil = millis() + TOAST_MS;
  int16_t w = t5Width(gToastMsg) + 16;
  int16_t x = (tft.width() - w) / 2, y = tft.height() - 38;
  tft.fillRoundRect(x, y, w, 22, 4, COL_TOAST_BG);
  tft.drawRoundRect(x, y, w, 22, 4, COL_DANGER);
  t5(x + 8, y + 7, gToastMsg, COL_TOAST_TEXT, COL_TOAST_BG);
}

void updateToast() {
  if (gToastUntil && (int32_t)(millis() - gToastUntil) >= 0) {
    gToastUntil = 0;
    paintScreen(gScreen);      /* repaints chrome AND forces a value refresh */
  }
}


/* `enabled = false` greys the button out — used where a stepper is already at
 * its end, so it is visible that this direction does nothing. */
void drawStepBtn(const JCRRect &r, bool pressed, bool plus, bool enabled) {
  uint16_t fill   = !enabled ? COL_DISABLED_FILL   : (pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  uint16_t border = !enabled ? COL_DISABLED_BORDER : COL_BOX_BORDER;
  uint16_t glyph  = !enabled ? COL_DISABLED_TEXT   : COL_TEXT_HI;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, border);
  /* Bar length is 55% of the box's short side, thickness an eighth of that,
   * both forced odd-ish so the two bars share a centre pixel. */
  int16_t s  = (r.w < r.h ? r.w : r.h);
  int16_t len = (int16_t)(s * 55 / 100);
  int16_t th  = (int16_t)(len / 4); if (th < 3) th = 3;
  int16_t cx = r.cx(), cy = (int16_t)(r.y + r.h / 2);
  tft.fillRect((int16_t)(cx - len / 2), (int16_t)(cy - th / 2), len, th, glyph);
  if (plus)
    tft.fillRect((int16_t)(cx - th / 2), (int16_t)(cy - len / 2), th, len, glyph);
}

void drawArrowBtn(const JCRRect &r, bool pressed, bool right) {
  uint16_t fill = pressed ? COL_BOX_PRESSED : COL_BOX_FILL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  /* Isosceles triangle, height ~55% of the short side, drawn as vertical
   * runs so it needs no triangle primitive: column k is (2k+1) px tall,
   * narrowing toward the tip. */
  int16_t s  = (r.w < r.h ? r.w : r.h);
  int16_t hh = (int16_t)(s * 55 / 100 / 2);          /* half height        */
  int16_t cx = r.cx(), cy = (int16_t)(r.y + r.h / 2);
  int16_t x0 = (int16_t)(cx - hh / 2);               /* centre the width   */
  for (int16_t k = 0; k <= hh; k++) {
    int16_t half = (int16_t)(hh - k);
    int16_t x = right ? (int16_t)(x0 + k) : (int16_t)(x0 + hh - k);
    tft.drawFastVLine(x, (int16_t)(cy - half), (int16_t)(2 * half + 1), COL_TEXT_HI);
  }
}

void t5Centered(const JCRRect &r, const char *s, uint16_t fg, uint16_t bg, uint8_t scale) {
  int16_t w = t5Width(s, scale);
  t5((int16_t)(r.x + (r.w - w) / 2), (int16_t)(r.y + (r.h - 7 * scale) / 2), s, fg, bg, scale);
}

/* ------------------------------------------------------------------------
 * Stepper readout. Same chrome as a mini stat box, but centred and cached so
 * holding a + button repaints four digits rather than the whole row.
 * ---------------------------------------------------------------------- */
void drawStepperBox(const JCRRect &r, const char *text, uint16_t fg, char *cache) {
  if (cache && strcmp(cache, text) == 0) return;
  if (cache) { strncpy(cache, text, 15); cache[15] = 0; }
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_FILL);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, COL_BOX_BORDER);
  tRussoCentered(RUSSO16, r.cx(), r.y + (r.h - RUSSO16.height) / 2, text, fg, COL_BOX_FILL);
}

/* ------------------------------------------------------------------------
 * ESC armed strip.
 * ---------------------------------------------------------------------- */
void escBarPaint() {
  tft.fillRect(0, 0, tft.width(), ESC_BAR_H, escArmed() ? COL_AMP : COL_BG);
}

void escBarTick() {
  static bool wasArmed = false;
  static bool first = true;
  bool now = escArmed();
  if (!first && now == wasArmed) return;
  first = false;
  wasArmed = now;
  escBarPaint();
}

/* ------------------------------------------------------------------------
 * Log strip, bottom edge. 0 = off, 1 = waiting (dashed), 2 = recording.
 * ---------------------------------------------------------------------- */
static uint8_t logBarMode() {
  LogState st = logState();
  if (st == LOGST_RECORDING) return 2;
  if (st == LOGST_WAITING)   return 1;
  return 0;
}

void logBarPaint() {
  int16_t y = (int16_t)(tft.height() - LOG_BAR_H);
  uint8_t m = logBarMode();
  tft.fillRect(0, y, tft.width(), LOG_BAR_H, m == 2 ? COL_DANGER : COL_BG);
  if (m == 1)
    for (int16_t x = 0; x < tft.width(); x += 24) tft.fillRect(x, y, 12, LOG_BAR_H, COL_DANGER);
}

void logBarTick() {
  static uint8_t was = 0xFF;
  uint8_t now = logBarMode();
  if (now == was) return;
  was = now;
  logBarPaint();
}

/* ------------------------------------------------------------------------
 * Test Mode widgets.
 * ---------------------------------------------------------------------- */
void drawLabelBtn(const JCRRect &r, bool pressed, const char *label, uint16_t accent,
                  bool filled, bool enabled) {
  uint16_t fill, border, text;
  if (!enabled) {
    fill = COL_DISABLED_FILL; border = COL_DISABLED_BORDER; text = COL_DISABLED_TEXT;
  } else {
    fill   = (filled != pressed) ? COL_BOX_PRESSED : COL_BOX_FILL;
    border = accent;
    text   = accent == COL_BOX_BORDER ? COL_TEXT_HI : accent;
  }
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, border);
  if (filled && enabled) tft.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, border);
  /* Scale 2 unless the label would overflow, then scale 1. */
  uint8_t sc = (t5Width(label, 2) <= r.w - 8) ? 2 : 1;
  t5Centered(r, label, text, fill, sc);
}

void drawTile(const JCRRect &r, const char *label, const char *value,
              bool selected, bool enabled) {
  uint16_t fill   = !enabled ? COL_DISABLED_FILL : (selected ? COL_BOX_PRESSED : COL_BOX_FILL);
  uint16_t border = !enabled ? COL_DISABLED_BORDER : (selected ? COL_VOLT : COL_BOX_BORDER);
  uint16_t lab    = !enabled ? COL_DISABLED_TEXT : COL_TEXT_HI;
  uint16_t val    = !enabled ? COL_DISABLED_TEXT : (selected ? COL_VOLT : COL_TEXT);
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, border);
  if (selected && enabled) tft.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, border);
  t5(r.x + 6, r.y + 5, label, lab, fill);
  gfxText.setFont(RUSSO16);
  const JCRFont &f = (gfxText.textWidth(value) <= r.w - 10) ? RUSSO16 : RUSSO13;
  tRussoCentered(f, r.cx(), r.y + 18 + (20 - f.height) / 2, value, val, fill);
}

void drawDeltaBtn(const JCRRect &r, bool pressed, const char *label, bool big,
                  bool enabled) {
  drawLabelBtn(r, pressed, label, big ? COL_BIG_STEP : COL_BOX_BORDER, false, enabled);
  if (big && enabled)
    tft.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, COL_BIG_STEP);
}

/* Horizontal arrow: a 3 px shaft from x0 to x1 with a solid head at the right
 * end, and at the left end too when `both`. Heads are vertical runs like
 * drawArrowBtn's, so no triangle primitive is needed. */
static void drawHArrow(int16_t x0, int16_t x1, int16_t cy, bool both, uint16_t c) {
  const int16_t HH = 7;                    /* head half-height = head length */
  int16_t sx0 = both ? (int16_t)(x0 + HH) : x0;
  tft.fillRect(sx0, (int16_t)(cy - 1), (int16_t)(x1 - HH - sx0 + 1), 3, c);
  for (int16_t k = 0; k <= HH; k++) {
    int16_t half = (int16_t)(HH - k);
    tft.drawFastVLine((int16_t)(x1 - HH + k), (int16_t)(cy - half), (int16_t)(2 * half + 1), c);
    if (both)
      tft.drawFastVLine((int16_t)(x0 + HH - k), (int16_t)(cy - half), (int16_t)(2 * half + 1), c);
  }
}

void drawEscTypeBtn(const JCRRect &r, bool pressed, bool bidi, bool enabled) {
  drawLabelBtn(r, pressed, "", COL_BOX_BORDER, false, enabled);
  uint16_t fill = !enabled ? COL_DISABLED_FILL : (pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  uint16_t ink  = !enabled ? COL_DISABLED_TEXT : COL_TEXT_HI;
  const char *txt = bidi ? "BIDIRECTIONAL" : "ONE DIRECTION";
  int16_t tw = t5Width(txt, 1);
  const int16_t AW = 34, GAP = 8;          /* icon width, icon-to-text gap  */
  int16_t x = (int16_t)(r.x + (r.w - (AW + GAP + tw)) / 2);
  int16_t cy = (int16_t)(r.y + r.h / 2);
  drawHArrow(x, (int16_t)(x + AW - 1), cy, bidi, ink);
  t5((int16_t)(x + AW + GAP), (int16_t)(cy - 3), txt, ink, fill, 1);
}
