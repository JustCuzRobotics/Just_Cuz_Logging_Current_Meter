/* ==========================================================================
 * JCR_TouchScreen — reliable touch + fast raw-SPI graphics for RP2040.
 * https://github.com/JustCuzRobotics/JCR_TouchScreen   MIT licensed.
 *
 * Umbrella header: include this and you get the display base class, the
 * ST7796 controller, the FT6336U touch engine, the text renderer and the
 * built-in 5x7 font.
 *
 *   #include <JCR_TouchScreen.h>
 *   JCR_ST7796 tft(PIN_CS, PIN_DC, PIN_RST, PIN_BL);
 *   JCR_FT6336 touch(Wire1, PIN_SDA, PIN_SCL, PIN_RST_T, PIN_INT);
 *   JCR_Text   text(tft);
 *
 * See README.md for wiring, the dual-core touch architecture, adding another
 * controller, and generating your own fonts.
 * ========================================================================*/
#pragma once

#include "JCR_TFT.h"
#include "JCR_ST7796.h"
#include "JCR_FT6336.h"
#include "JCR_Text.h"
#include "fonts/Font5x7.h"

#define JCR_TOUCHSCREEN_VERSION "1.0.0"
