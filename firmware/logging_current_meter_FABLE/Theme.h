/* ==========================================================================
 * Theme.h — the active palette, and the fonts.
 *
 * The COL_* names are macros that dereference the *current* palette rather
 * than constants, so switching theme is a pointer assignment plus a repaint
 * and no screen file needs to know themes exist. The extra indirection costs
 * nothing next to the SPI write every colour ends up feeding.
 *
 * Two palettes. PAL_DARK is minimal-ink: background and box fill are both
 * pure black and a box is defined only by its border and text. PAL_CLASSIC
 * is the v3.0 palette with filled slate panels.
 *
 * A note for the record, because it shaped every palette decision before
 * v3.1b: the display was colour-INVERTED from v2.0 until v3.1b (the IPS panel
 * needs INVON and the driver sent INVOFF), so every "this looks washed out on
 * the glass" judgement in that period was made on the complement of the
 * palette. The panel was never the problem. Judge both palettes fresh.
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>

struct Palette {
  const char *name;
  uint16_t bg, grid, text, textHi;
  uint16_t volt, amp, temp;              /* channel accents — same in both  */
  uint16_t boxFill, boxBorder, boxPressed;
  uint16_t disabledFill, disabledBorder, disabledText;
  uint16_t danger, toastBg, toastText;
  uint16_t bigStep;                      /* the coarse button of a small/big pair */
};

extern const Palette PAL_DARK;      /* minimal ink: no filled panels        */
extern const Palette PAL_CLASSIC;   /* the v3.0 palette                     */
extern const Palette *gPal;         /* what everything below resolves to    */

enum ThemeId : uint8_t { THEME_DARK = 0, THEME_CLASSIC = 1, THEME_COUNT = 2 };
void themeApply(uint8_t id);        /* sets gPal; does NOT repaint          */
uint8_t themeCurrent();

#define COL_BG              (gPal->bg)
#define COL_GRID            (gPal->grid)
#define COL_TEXT            (gPal->text)
#define COL_TEXT_HI         (gPal->textHi)
#define COL_VOLT            (gPal->volt)
#define COL_AMP             (gPal->amp)
#define COL_TEMP            (gPal->temp)
#define COL_BOX_FILL        (gPal->boxFill)
#define COL_BOX_BORDER      (gPal->boxBorder)
#define COL_BOX_PRESSED     (gPal->boxPressed)
#define COL_DISABLED_FILL   (gPal->disabledFill)
#define COL_DISABLED_BORDER (gPal->disabledBorder)
#define COL_DISABLED_TEXT   (gPal->disabledText)
#define COL_DANGER          (gPal->danger)
#define COL_TOAST_BG        (gPal->toastBg)
#define COL_TOAST_TEXT      (gPal->toastText)
#define COL_BIG_STEP        (gPal->bigStep)

/* Russo One, rasterized by the library's extras/make_fonts.py. Russo One is
 * SIL Open Font Licensed; these headers are generated from it locally rather
 * than shipped with the library. RUSSOBIG is monospaced so a value that
 * updates in place always occupies the same cell. */
#include "RussoOne13.h"    /* narrow tile labels        */
#include "RussoOne16.h"    /* screen titles             */
#include "RussoOne22.h"    /* big tiles, action buttons */
#include "RussoOneBig.h"   /* live values, tabular      */
