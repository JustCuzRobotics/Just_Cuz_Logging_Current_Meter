/* ==========================================================================
 * Theme.h — colours and fonts. The dark tones are deliberately deeper than
 * a naive dark theme: on this panel a mid-slate box fill reads as a washed
 * light blue, so the fills sit just above the background and the borders do
 * the work of defining each box.
 *
 * To go darker still, move BOX_FILL/BOX_PRESSED toward COL_BG. For more box
 * definition, brighten COL_BOX_BORDER.
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>

#define COL_BG              0x0020   /* #040408 near-black                 */
#define COL_GRID            0x18E3   /* plot gridlines                     */
#define COL_TEXT            0xDF3D
#define COL_TEXT_HI         0xEF7E
#define COL_VOLT            0x3F18   /* teal                               */
#define COL_AMP             0xFD84   /* amber                              */
#define COL_TEMP            0xFA73   /* magenta                            */
#define COL_BOX_FILL        0x0882   /* deep slate                         */
#define COL_BOX_BORDER      0x2A6B
#define COL_BOX_PRESSED     0x1945   /* press / "on" feedback              */
#define COL_DISABLED_FILL   0x0841
#define COL_DISABLED_BORDER 0x2146
#define COL_DISABLED_TEXT   0x4AAB
#define COL_DANGER          0xFB4B
#define COL_TOAST_BG        0x0861
#define COL_TOAST_TEXT      0xFBEF

/* Russo One, rasterized by the library's extras/make_fonts.py. Russo One is
 * SIL Open Font Licensed; these headers are generated from it locally rather
 * than shipped with the library. RUSSOBIG is monospaced so a value that
 * updates in place always occupies the same cell. */
#include "RussoOne13.h"    /* narrow tile labels        */
#include "RussoOne16.h"    /* screen titles             */
#include "RussoOne22.h"    /* big tiles, action buttons */
#include "RussoOneBig.h"   /* live values, tabular      */
