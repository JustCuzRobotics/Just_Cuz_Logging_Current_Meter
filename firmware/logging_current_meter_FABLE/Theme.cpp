#include "Theme.h"

/* Minimal ink, HIGH contrast. bg and boxFill are both pure black on purpose:
 * a "box" here is a border and its text, nothing more, so the lit area of the
 * screen drops to the strokes themselves.
 *
 * The first attempt at this failed on hardware and the reason is worth
 * recording. With no fill behind them, dim borders left nothing on screen
 * bright enough to read as structure — the panel showed a flat wash with only
 * the warm accents visible, which looks like a yellow tinge rather than a
 * dark theme. Removing fill means the strokes have to carry ALL the
 * definition, so borders and text here are far brighter than the classic
 * palette's, not dimmer. Dark mode is high contrast, not low.
 */
const Palette PAL_DARK = {
  "DARK",
  /* bg            */ 0x0000,
  /* grid          */ 0x2104,
  /* text          */ 0xFFFF,
  /* textHi        */ 0xFFFF,
  /* volt          */ 0x3F18,
  /* amp           */ 0xFD84,
  /* temp          */ 0xFA73,
  /* boxFill       */ 0x0000,
  /* boxBorder     */ 0x8410,   /* 50% grey — doing all the structural work */
  /* boxPressed    */ 0x39E7,
  /* disabledFill  */ 0x0000,
  /* disabledBorder*/ 0x39E7,
  /* disabledText  */ 0x6B4D,
  /* danger        */ 0xFB4B,
  /* toastBg       */ 0x0000,
  /* toastText     */ 0xFBEF,
  /* bigStep       */ 0x87F0,   /* lime: unlike any channel accent */
};

/* Navy. The v3.0 "classic" ground was 0x0020, which in RGB565 is not a dark
 * blue at all — it is the green LSB, and the whole palette read green once
 * the inversion was fixed. This one is built from real RGB: a #060E28 ground,
 * #0E1C42 panels, #3A6096 borders — the deep-blue instrument look of the
 * WM150 this meter is meant to sit beside. */
const Palette PAL_CLASSIC = {
  "CLASSIC",
  /* bg            */ 0x0065,
  /* grid          */ 0x1109,
  /* text          */ 0xDF3D,
  /* textHi        */ 0xEF7E,
  /* volt          */ 0x3F18,
  /* amp           */ 0xFD84,
  /* temp          */ 0xFA73,
  /* boxFill       */ 0x08E8,
  /* boxBorder     */ 0x3B12,
  /* boxPressed    */ 0x19AD,
  /* disabledFill  */ 0x08A6,
  /* disabledBorder*/ 0x21AB,
  /* disabledText  */ 0x4B31,
  /* danger        */ 0xFB4B,
  /* toastBg       */ 0x08A6,
  /* toastText     */ 0xFBEF,
  /* bigStep       */ 0x87F0,   /* lime: unlike any channel accent */
};

static const Palette *const kPalettes[THEME_COUNT] = { &PAL_DARK, &PAL_CLASSIC };
static uint8_t s_theme = THEME_DARK;

const Palette *gPal = &PAL_DARK;

void themeApply(uint8_t id) {
  if (id >= THEME_COUNT) id = THEME_DARK;
  s_theme = id;
  gPal = kPalettes[id];
  /* Announced because "the theme did not change" and "the theme changed but
   * both look alike on this panel" are different problems with the same
   * symptom, and only one of them is a code bug. */
  if (Serial) Serial.printf("# [theme] %s  bg=0x%04X boxFill=0x%04X border=0x%04X\n",
                            gPal->name, gPal->bg, gPal->boxFill, gPal->boxBorder);
}
uint8_t themeCurrent() { return s_theme; }
