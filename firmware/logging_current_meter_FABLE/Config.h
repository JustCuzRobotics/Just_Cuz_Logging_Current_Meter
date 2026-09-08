/* ==========================================================================
 * Config.h — everything you would change to move this firmware to different
 * hardware: pin map, bus rates, sampling cadence, calibration constants.
 *
 * Calibration values are produced by display_bringup.ino's serial v/i/n
 * routines; paste fresh numbers here and reflash. The Calibrate screen shows
 * them read-only along with whether each has actually been fitted.
 * ========================================================================*/
#pragma once
#include <Arduino.h>

/* ---- pin map (Waveshare RP2040-Zero + 3.5" ST7796/FT6336U on one FPC) ---- */
#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_RS    6      /* DC: data/command select                    */
#define PIN_LCD_RST   7
#define PIN_LCD_LED   8      /* solid HIGH only — never PWM (see below)    */
#define PIN_SD_CS     9      /* park HIGH: shares the FPC's MISO           */
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13
#define PIN_I_SENSE  26      /* ADC0 — ACS770 current sensor               */
#define PIN_V_PACK   27      /* ADC1 — pack voltage divider                */
#define PIN_T_SENSE  28      /* ADC2 — NTC thermistor divider              */
#define PIN_V5_SENSE 29      /* ADC3 — +5V rail / 2, ratiometric reference */
#define PIN_ESC_SIG   1      /* ESC servo signal out — hardware PWM slice   */

/* ---- display / touch ---- */
#define LCD_ROTATION   1            /* 1 = 480x320 landscape               */
#define LCD_IPS        1            /* IPS panel: needs INVON or every colour
                                     * displays as its complement. The lcdwiki
                                     * MSP3526 / Hosyond 3.5" is IPS.         */
#define SPI_HZ         40000000UL
#define TOUCH_I2C_HZ   400000
#define TOUCH_HZ       200          /* touch samples per second on core 1  */
#define TOUCH_NATIVE_W 320          /* the touch panel's own portrait size */
#define TOUCH_NATIVE_H 480
#define TOUCH_INVERT_X false        /* flip if the crosshair mirrors your  */
#define TOUCH_INVERT_Y false        /* finger — crosshair is ground truth  */

/* ---- sampling ---- */
#define ADC_MAX          4095.0f
#define G2_OVER_G1      0.755319f   /* cancels VCC/VREF in the current ratio */
#define ADC_AVG               64    /* oversample count (RP2040 ADC DNL)     */
#define TICK_US            13158UL  /* 5 s / 380 graph columns               */

/* ---- calibration (paste from display_bringup's v/i/n routines) ---- */
#define V_GAIN_CAL       0.01662778f   /* volts per ADC count             */
#define V_OFFSET_CAL    -0.13632f      /* volts                           */
#define V_CAL_VALID      1
#define I_QUIESCENT_CAL  0.105399f     /* ratio at 0 A — MEASURED         */
#define I_ZERO_VALID     1
#define I_SENS_CAL       0.00533200f   /* ratio per amp — NOMINAL         */
#define I_GAIN_VALID     0
#define RV1_OHMS       100830.0f
#define NTC_B            3836.6f
#define NTC_R25         97988.0f

/* ---- UI frame gates ---- */
#define LIVE_FRAME_MS   50
#define GRAPH_FRAME_MS  33
#define DEV_FRAME_MS    50
#define TOAST_MS        1600

/* Two hardware rules that are not negotiable:
 *  - PIN_SD_CS must be driven HIGH before anything else touches the SPI bus.
 *    It shares the FPC's MISO; floating low, the card fights the display.
 *  - PIN_LCD_LED is a solid level, never analogWrite(). V5_SENSE only passes
 *    to ~318 Hz, so PWM at ~1 kHz injects current noise no calibration
 *    removes (DESIGN.md section 4.2). */
