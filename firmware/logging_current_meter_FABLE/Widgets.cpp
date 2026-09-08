#include "Widgets.h"
#include "Screens.h"

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
