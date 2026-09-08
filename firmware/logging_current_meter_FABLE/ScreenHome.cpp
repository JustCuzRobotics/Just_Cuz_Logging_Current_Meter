/* ScreenHome.cpp — five tiles, vector glyphs, nothing per-tick. */
#include "Screens.h"
#include "Widgets.h"
#include <math.h>

/* Small vector glyphs stand in for icons the bitmap font can't render. */
static void drawHomeGlyph(uint8_t id, int16_t cx, int16_t cy, uint16_t color) {
  switch (id) {
    case HOME_LIVE:  tft.fillCircle(cx, cy, 7, color); break;
    case HOME_GRAPH:
      tft.drawLine(cx - 12, cy + 4, cx - 4, cy - 6, color);
      tft.drawLine(cx - 4,  cy - 6, cx + 4, cy + 2, color);
      tft.drawLine(cx + 4,  cy + 2, cx + 12, cy - 8, color);
      break;
    case HOME_LOG:   tft.drawRect(cx - 7, cy - 7, 14, 14, color); break;
    case HOME_CAL:
      tft.drawCircle(cx, cy, 7, color);
      for (uint8_t k = 0; k < 6; k++) {
        float a = k * 3.14159f / 3.0f;
        tft.drawLine(cx + (int16_t)(cosf(a) * 9),  cy + (int16_t)(sinf(a) * 9),
                     cx + (int16_t)(cosf(a) * 12), cy + (int16_t)(sinf(a) * 12), color);
      }
      break;
    case HOME_DEV:
      tft.drawFastHLine(cx - 10, cy, 21, color);
      tft.drawFastVLine(cx, cy - 10, 21, color);
      tft.drawCircle(cx, cy, 4, color);
      break;
  }
}

static void paintHomeTile(uint8_t id, bool pressed) {
  const JCRRect &r = HOME_T[id].vis;
  bool disabled = HOME_DISABLED[id];
  uint16_t fill     = disabled ? COL_DISABLED_FILL   : (pressed ? COL_BOX_PRESSED : COL_BOX_FILL);
  uint16_t border   = disabled ? COL_DISABLED_BORDER : (pressed ? COL_VOLT        : COL_BOX_BORDER);
  uint16_t textCol  = disabled ? COL_DISABLED_TEXT   : COL_TEXT;
  uint16_t glyphCol = disabled ? COL_DISABLED_TEXT   : COL_VOLT;

  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  drawHomeGlyph(id, r.cx(), r.y + 32, glyphCol);

  /* The two wide tiles take the large face; the narrow three use the small
   * one so "CALIBRATE" fits inside 137 px. */
  bool big = (id == HOME_LIVE || id == HOME_GRAPH);
  tRussoCentered(big ? RUSSO22 : RUSSO13, r.cx(), r.y + (big ? 50 : 56),
                 HOME_LABEL[id], textCol, fill);
  t5(r.x + (r.w - t5Width(HOME_SUB[id])) / 2, r.y + 86, HOME_SUB[id],
     disabled ? COL_DISABLED_TEXT : COL_TEXT_HI, fill);
}

void paintHomeOnce() {
  tft.fillScreen(COL_BG);
  for (uint8_t i = 0; i < HOME_N; i++) paintHomeTile(i, false);
}

void homeSetPressed(int8_t id, bool pressed) { paintHomeTile((uint8_t)id, pressed); }

void homeDispatch(int8_t id) {
  if (HOME_DISABLED[id]) { showToast("Not implemented in this build"); return; }
  switch (id) {
    case HOME_LIVE:  goTo(SCR_LIVE);  break;
    case HOME_GRAPH: goTo(SCR_GRAPH); break;
    case HOME_CAL:   goTo(SCR_CAL);   break;
    case HOME_DEV:   goTo(SCR_DEV);   break;
  }
}
