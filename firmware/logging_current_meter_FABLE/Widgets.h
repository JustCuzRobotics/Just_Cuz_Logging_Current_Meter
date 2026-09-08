/* ==========================================================================
 * Widgets.h — shared chrome and the cached-field discipline.
 *
 * THE REDRAW RULE, since every screen depends on it: paint static chrome once
 * on entry, then per tick redraw only the fields whose text actually changed.
 * A full-screen fill is ~60 ms even at 40 MHz and blocks core 0 for all of it.
 *
 * Cached fields compare against the last string drawn and do nothing if it
 * matches. That means `forceClear` MUST be passed true the first time a
 * screen's tick runs after anything wiped the pixels underneath — screen
 * entry, or a toast expiring and repainting chrome. Otherwise the cache still
 * says "already drew this", a value that happens not to change stays blank,
 * and the field appears to vanish. (That was a real bug: numbers disappearing
 * after pressing a reset button, which is exactly when nothing else moves.)
 * ========================================================================*/
#pragma once
#include <JCR_TouchScreen.h>
#include "Config.h"
#include "Theme.h"
#include "Layout.h"

extern JCR_ST7796 tft;
extern JCR_Text   gfxText;

/* ---- text helpers (the 5x7 font at an integer scale) ---- */
void t5(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale = 1);
int16_t t5Width(const char *s, uint8_t scale = 1);
int16_t bigCellW();
void tRusso(const JCRFont &f, int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg);
void tRussoCentered(const JCRFont &f, int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg);

/* ---- cached fields. cache == nullptr means "always redraw". ---- */
bool field5(int16_t x, int16_t y, uint8_t maxChars, uint8_t scale,
            uint16_t fg, uint16_t bg, const char *text, char *cache);
bool fieldBig(int16_t x, int16_t y, uint8_t maxChars,
              uint16_t fg, uint16_t bg, const char *text, char *cache);

/* ---- button / box chrome ---- */
void drawBackBtn(const JCRRect &r, bool pressed);
void drawBackBtnCompact(const JCRRect &r, bool pressed);
void drawIconBtn(const JCRRect &r, bool pressed, char glyph);
void drawActionBtn(const JCRRect &r, bool pressed, const char *label);
void drawChip(const JCRRect &r, char letter, uint16_t color, bool on, int16_t textInset);
void drawMiniStatChrome(const JCRRect &r, const char *label);
void drawMiniStatValue(const JCRRect &r, uint16_t color, const char *text,
                       uint8_t textSize, char *cache);
void drawBigValueChrome(const JCRRect &r);
bool drawBigValueField(const JCRRect &r, uint16_t valColor, const char *valText,
                       const char *unitText, char *cache);

/* ---- steppers ----
 * drawStepBtn draws its + or - as filled bars rather than a font glyph. A
 * 5x7 '+' in a 44 px button is a speck, and scaling a bitmap glyph up gets
 * chunky fast; two rectangles stay crisp at any size and scale with the box,
 * so the same call works for a 24 px chip and a 44 px stepper. */
void drawStepBtn(const JCRRect &r, bool pressed, bool plus);

/* ---- stepper value box: the readout between a - and a + button ---- */
void drawStepperBox(const JCRRect &r, const char *text, uint16_t fg, char *cache);

/* 5x7 text centred in a box — for labels that must not overflow, where a
 * proportional font's width is a guess and 6*len*scale is not. */
void t5Centered(const JCRRect &r, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

/* ---- ESC armed indicator ----
 * A 4 px amber strip along the top edge, on every screen, whenever the ESC
 * output is live. It is a strip rather than a labelled badge because every
 * screen already spends its top corners, and a warning that a motor may spin
 * has to be somewhere it can never be crowded out. escBarPaint() draws the
 * current state unconditionally; escBarTick() redraws only on a change and
 * is cheap enough to call every frame. */
void escBarPaint();
void escBarTick();

/* ---- toast: bottom-centre, timed, non-blocking ---- */
void showToast(const char *msg);
void updateToast();
