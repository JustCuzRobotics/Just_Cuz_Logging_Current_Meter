/* ============================================================================
 * RC Logging Current Meter - Rev A board bring-up diagnostic
 * ----------------------------------------------------------------------------
 *   VERSION 2.1          LAST UPDATED 2026-08-25 22:40 EDT
 *
 *   The banner printed at boot repeats this stamp AND the compiler's own build
 *   time, so you can confirm from the serial log that the board is running the
 *   file you think it is.  If FW_UPDATED below does not match what you expect,
 *   you have the wrong copy; if the BUILT time is stale, the IDE did not
 *   recompile.
 *
 *   2.1  2026-08-25  Thermistor refined: B=3836.6 from the run with BOTH
 *                    ends verified, R25=97988 still anchored on the dry
 *                    FNIRSI point. Taking each parameter from the run that
 *                    measured it best removes a systematic ~0.7 C warm bias.
 *   2.0  2026-08-25  Thermistor FITTED: B=3893.8, R25=97804, from 22.1 C
 *                    and 99.0 C. The ice bath was abandoned - three tries
 *                    all produced resistances implying ~16 C, and 77 C of
 *                    spread settles B just as well. All four analog
 *                    channels now have measured constants.
 *   1.9  2026-08-25  NTC fit now rejects physically implausible results
 *                    (+/-15% on B, +/-25% on R25) instead of printing them
 *                    as if they were usable, and says what to check. A fit
 *                    returning R25=57k for a part the DMM reads at 111k
 *                    at 22 C is bad DATA, not a bad part. Also warns to
 *                    seal the probe - bare leads in water shunt the
 *                    thermistor and break the cold point specifically.
 *   1.8  2026-08-25  FIXED: v1.7 baked the NOMINAL quiescent 0.1 rather than
 *                    this board's measured 0.105399, so the board read a
 *                    phantom 1.01 A with nothing connected. Current zero and
 *                    gain are now tracked separately, because the zero is
 *                    calibrated and the gain is not - reporting both as one
 *                    flag was hiding which number to trust.
 *   1.7  2026-08-25  Calibration constants BAKED IN rather than living in
 *                    RAM: V_GAIN_CAL / V_OFFSET_CAL from the 9.99 V and
 *                    57.03 V fit now survive a reflash. New 'c' command and
 *                    a boot line show what is stored and which channels are
 *                    genuinely calibrated. Current still nominal - waiting
 *                    on the numbers from 'i'.
 *   1.6  2026-08-25  V_PACK check reworded - it is a safety interlock, and
 *                    reporting FAIL during a deliberate voltage calibration
 *                    read like a board fault.
 *   1.5  2026-08-25  Backlight is now driven SOLID HIGH, never PWM'd, and
 *                    SW1 no longer dims it. PWM at ~1 kHz sits in the gap
 *                    between I_SENSE's 10 kHz and V5_SENSE's 318 Hz, where
 *                    the 4.2 ratiometric correction cannot cancel it - it
 *                    was injecting uncorrectable current noise.
 *   1.4  2026-08-25  Graph sampling moved to CORE 1 (DESIGN.md section 11's
 *                    own split). v1.3 managed only ~25 Hz because a 31 ms
 *                    draw blocked the sampler and missed samples were
 *                    dropped, so 388 columns spanned ~15 s on an axis
 *                    labelled 5 s. Interval is now microseconds with
 *                    catch-up, and the ACHIEVED rate is measured and shown
 *                    on the panel rather than assumed.
 *   1.3  2026-08-25  Live V/I graph, command 'g': 5 s scrolling window,
 *                    volts blue on the left axis, amps red on the right,
 *                    2 px traces, autoscaling. Touch inverts reverted to 0
 *                    (the crosshair under the finger is the ground truth,
 *                    not a verbal description). Crosshair enlarged to a
 *                    ring plus long arms, and 'x' now clears the screen so
 *                    erasing with black stops punching holes in the test
 *                    pattern.
 *   1.2  2026-08-25  MISO test downgraded from FAIL to INFO when it reads
 *                    0xFF. With no SD card in the slot nothing drives that
 *                    pin on these modules, so 0xFF is the expected result
 *                    on a healthy board with a working panel. S11 can only
 *                    be answered with a card inserted.
 *   1.1  2026-08-25  Touch responsiveness. Root cause was FT6336U register
 *                    0x86 - monitor mode is ALLOWED by default and 0x87
 *                    drops into it after 30 s idle. Now forbidden, scan
 *                    period set fastest, I2C raised to 400 kHz, and CTP_INT
 *                    (GP13) drives an ISR flag so reads are event-driven
 *                    with a 50 ms timed fallback. Reports how each sample
 *                    was triggered so a dead CTP_INT is visible.
 *   1.0  2026-08-25  Touch mapping SOLVED on hardware: TOUCH_INVERT_X and
 *                    TOUCH_INVERT_Y both 1 at LCD_ROTATION 1. Display,
 *                    touch, all four ADC channels and the thermistor are
 *                    now all working. Buttons remain dead (footprint bug).
 *   0.9  2026-08-25  FIXED: the Enter that starts a command was being read
 *                    as a second keypress one iteration later. That killed
 *                    the 'x' and 'A' streams instantly, and made 'n'/'v'/'i'
 *                    abort on an empty prompt. CR/LF is now ignored as a
 *                    keypress and drained after each command. 'x' also runs
 *                    the touch scan itself if it has not run yet.
 *   0.8  2026-08-25  Pack-voltage calibration ('v', two-point linear with
 *                    offset) and current calibration ('i', tare then gain,
 *                    plus an optional check point). Both applied live for
 *                    the session; adcReport marks readings CALIBRATED or
 *                    nominal.
 *   0.7  2026-08-25  Two-point NTC calibration added, command 'n'. Solves
 *                    R25 and B from an ice bath and boiling water, and
 *                    prints the #defines to paste back in.
 *   0.6  2026-08-25  RV1_OHMS refined to 100.83k on an FNIRSI 2C53T.
 *                    NTC confirmed ~2.4% below nominal R25 across two
 *                    room temperatures - a real part offset, not noise.
 *                    Left NTC_R25 nominal pending an ice-bath fit.
 *   0.5  2026-08-25  RV1_OHMS set to the measured 100.7k (was a nominal
 *                    100k). Thermistor now agrees with a reference meter to
 *                    +0.43 C at room temperature.
 *   0.4  2026-08-25  T_SENSE pass band widened - v0.3 wrongly FAILED a
 *                    correctly working NTC reading 25.5 C, because the band
 *                    assumed nothing was plugged in. Open and connected are
 *                    both passes now; only a near-zero reading is a fault.
 *   0.3  2026-08-25  Version stamp added.
 *   0.2  2026-08-25  Board tests promoted ahead of the display and made
 *                    independent of it. All 20 GPIO now exercised: 4 ADC
 *                    channels with computed pass/fail windows, ratiometric
 *                    zero-current check, button bounce measurement, GPIO
 *                    bridge/short test, ESC output, GP0 loopback. Serial
 *                    command menu. gfx->begin() no longer reported as a pass.
 *   0.1  2026-08-24  Display and touch only.
 * ----------------------------------------------------------------------------
 * Just 'Cuz Robotics.  Waveshare RP2040-Zero on the Logging_Current_Meter Rev A
 * PCB.  Display: lcdwiki MSP3526 / Hosyond 3.5" 480x320 IPS, ST7796U + FT6336U
 * capacitive touch + microSD on a shared 14-way 0.5 mm FPC.
 *
 * EVERYTHING REPORTS OVER USB SERIAL AT 115200.  The display is treated as just
 * one more subsystem under test - if the panel is dead, every other test still
 * runs and still reports.  Do not rely on the screen to tell you anything.
 *
 *   !! USB POWER ONLY.  Nothing in the 60 V current path while running this. !!
 *   The sketch checks V_PACK at boot and shouts if it sees pack voltage.
 *
 * ---------------------------------------------------------------------------
 * Pin map - mirrors EXPECTED_MCU in generator/verify.py.  All 20 usable GPIO.
 *
 *   GP0   spare, brought out to J13 pad
 *   GP1   ESC_SIG_MCU -> J8 pin 1
 *   GP2   MCU_SCK  -> R11 -> FPC 7
 *   GP3   MCU_MOSI -> R12 -> FPC 6
 *   GP4   MCU_MISO <- R13 <- FPC 9
 *   GP5   LCD_CS   -> FPC 3
 *   GP6   LCD_RS   -> FPC 5        D/C: high = data, low = command
 *   GP7   LCD_RST  -> FPC 4        low = reset
 *   GP8   MCU_LED  -> R14 -> FPC 8 backlight, transistor gate, PWM
 *   GP9   SD_CS    -> FPC 14       parked HIGH always
 *   GP10  CTP_SDA  -> FPC 12       I2C1
 *   GP11  CTP_SCL  -> FPC 10       I2C1
 *   GP12  CTP_RST  -> FPC 11       low = reset
 *   GP13  CTP_INT  <- FPC 13
 *   GP14  BTN1 (SW1, MODE)         R15 not fitted -> internal pull-up
 *   GP15  BTN2 (SW2, ZERO/TARE)    R16 not fitted -> internal pull-up
 *   GP16  onboard WS2812 (RP2040-Zero)
 *   GP26  I_SENSE   ADC0   ACS770 VIOUT via R1/R2   (G1 = 0.661972)
 *   GP27  V_PACK    ADC1   pack voltage via 21:1    (R3/R4/R6, R7, D1, C5)
 *   GP28  T_SENSE   ADC2   NTC divider              (R10, RV1, R9, C7)
 *   GP29  V5_SENSE  ADC3   +5V rail / 2             (R5/R8, C6)
 *
 * ---------------------------------------------------------------------------
 * Toolchain
 *   Board:   arduino-pico core by Earle Philhower -> "Waveshare RP2040 Zero"
 *   Library: "GFX Library for Arduino" by moononournation  (Arduino_GFX)
 *            NOT Adafruit_GFX - different library, different header.
 * ==========================================================================*/

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>

/* Set to 1 and install "Adafruit NeoPixel" to use the RP2040-Zero's onboard
 * WS2812 as a status light.  Useful precisely because the panel may be dead.
 * Left off by default so the sketch compiles with no extra dependency.      */
#define USE_NEOPIXEL 0
#if USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel px(1, 16, NEO_GRB + NEO_KHZ800);
#endif

/* ------------------------------------------------------------------ version
 * FW_UPDATED is set by hand when this file is edited.  BUILT comes from the
 * compiler, so a stale BUILT time means the IDE reused a cached object file
 * and the board is NOT running what you just changed.                        */
#define FW_VERSION  "2.1"
#define FW_UPDATED  "2026-08-25 22:40 EDT"
#define FW_BUILT    __DATE__ " " __TIME__

/* ---------------------------------------------------------------- pin map */
#define PIN_GP0       0
#define PIN_ESC_SIG   1
#define PIN_SCK       2
#define PIN_MOSI      3
#define PIN_MISO      4
#define PIN_LCD_CS    5
#define PIN_LCD_RS    6
#define PIN_LCD_RST   7
/* Backlight. DRIVEN SOLID HIGH in normal operation - never PWM'd.
 *
 * Not a brightness preference, an accuracy one. I_SENSE passes to 10 kHz (C4)
 * but V5_SENSE only to 318 Hz (C6), so DESIGN.md 4.2's ratiometric correction
 * is only valid below ~318 Hz. analogWrite's default ~1 kHz PWM lands inside
 * that gap: it modulates VBUS, the sensor output follows it because the part
 * is ratiometric and fast, and V5_SENSE cannot see it to cancel it. The result
 * is current noise that no amount of calibration removes.
 *
 * A CONSTANT load is the ideal case - a DC shift in VBUS cancels exactly,
 * which is what 4.2 exists to do. 100% costs ~100 mA of a 500 mA USB budget
 * (161 mA total for the board), and gives the brightest panel for filming.  */
#define PIN_LCD_LED   8
#define PIN_SD_CS     9
#define PIN_CTP_SDA  10
#define PIN_CTP_SCL  11
#define PIN_CTP_RST  12
#define PIN_CTP_INT  13
#define PIN_BTN1     14
#define PIN_BTN2     15
#define PIN_I_SENSE  26
#define PIN_V_PACK   27
#define PIN_T_SENSE  28
#define PIN_V5_SENSE 29

/* ------------------------------------------------------- analog constants */
/* From DESIGN.md sections 4.1 - 4.4.  These are the design values, not
 * calibrated ones - section 6 calibration has not been done yet.            */
#define ADC_MAX        4095.0f
#define ADC_VREF          3.3f    /* RP2040 ADC ref = +3V3 rail              */
#define G1            0.661972f   /* R2/(R1+R2) = 4.7/7.1, current divider   */
#define G2                0.5f    /* R8/(R5+R8), 5 V rail divider            */
#define G2_OVER_G1    0.755319f
#define ACS_SENS      0.005332f   /* (VIOUT/VCC) per amp                     */
#define ACS_QUIESCENT     0.1f    /* VIOUT/VCC at 0 A                        */
#define VPACK_RATIO      21.0f    /* (100k+100k+10k)/10k                     */
/* FITTED 2026-08-25. Two parameters, taken from the two runs that measured
 * each one best, rather than from a single pair of points.
 *
 *   B   from 21.1 C + 100.0 C, both verified with a meat thermometer.
 *       Corroborated independently by a 22.1 C + 97 C run at B=3834.9.
 *       Two runs agreeing to 0.05% is why this value is trusted.
 *
 *   R25 anchored on 111.190 k at 22.1 C - FNIRSI 2C53T, in air, DRY, against
 *       a reference meter. Every wet measurement is shunted by a few hundred
 *       kohm through the leads, so the dry point is the only clean one.
 *       Agrees with the 97.99 k from the 22.1/97 run to 0.2%.
 *
 * The 21.1 C + 100 C run's own R25 came out 94.9 k, about 3% low, because a
 * meat thermometer is built for 100 C and is worth roughly a degree at room
 * temperature - and R25 moves ~4.5% per degree there. Hence splitting the
 * sources: its B is excellent, its R25 is not.
 *
 * ABANDONED: the ice bath. Three attempts produced resistances implying
 * ~16 C. Wet leads shunt the thermistor by ~340 k, which is a 49% error at
 * 0 C and only 2% at 100 C - a temperature-dependent error that no 2-parameter
 * fit can absorb, which is exactly why every ice-bath fit disagreed with the
 * last. This fit PREDICTS 308 k at 0 C; measure that as a CHECK if a real ice
 * slurry ever happens, never as an input.                                   */
#define NTC_B            3836.6f
#define NTC_R25         97988.0f
#define RV1_OHMS       100830.0f  /* MEASURED 2026-08-25 with a DMM across the
                                   * pot legs, power off, NTC unplugged, on
                                   * an FNIRSI 2C53T. A measured circuit
                                   * constant, not a fit. Note the reading is
                                   * barely critical: 100.70k vs 100.83k moves
                                   * the temperature by 0.03 C. The NTC's own
                                   * R25 tolerance dominates by 20x.
                                   * Re-measure if RV1 is ever turned.        */

/* =============================== STORED CALIBRATION ========================
 * Measured on THIS board. Baked in so they survive a reflash - without this
 * the 'v' / 'i' fits live in RAM only and are lost on every upload.
 *
 * Re-run the fit and paste new values here after ANY change to a divider,
 * the LDO, or the sensor. Set the _VALID flag to 1 when a channel has been
 * genuinely calibrated, so the report can tell you which numbers to trust.
 *
 * VOLTAGE - fitted 2026-08-25, two points at 9.990 V and 57.030 V
 *   gain is -1.74% from nominal, which is the 3V3 LDO tolerance landing on a
 *   GND-referenced channel exactly as DESIGN.md 4.3 / 5 predict.
 *   The offset is REAL: 8.2 ADC counts. A one-point fit through the origin,
 *   which DESIGN.md 6 step 2 currently specifies, would read +1.12% high at
 *   10 V and worse below that.                                             */
#define V_GAIN_CAL       0.01662778f  /* V per ADC count                     */
#define V_OFFSET_CAL    -0.13632f     /* V                                   */
#define V_CAL_VALID      1

/* CURRENT - ZERO and GAIN are separate, because they are separately valid.
 *
 * ZERO: measured 2026-08-25 on this board, ratio 0.105399 at zero current.
 * That is +5.40% from the nominal 0.1 - ordinary ACS770 quiescent-output
 * tolerance plus divider tolerance. v1.7 shipped the NOMINAL 0.1 by mistake,
 * which made the board read a phantom 1.01 A with nothing connected.
 *
 * Still re-tare at the start of a session: DESIGN.md 6 step 1 notes the
 * ACS770 retains up to 400 mA of magnetic offset after a large excursion,
 * and this stored value is a starting point, not a substitute.
 *
 * GAIN: still nominal. Record I_CAL_CURRENT when you fit it - the +/-1.5 A
 * nonlinearity floor is +/-5% of a 30 A fit but +/-37% of a 4 A one (5).   */
#define I_QUIESCENT_CAL  0.105399f    /* ratio at 0 A - MEASURED            */
#define I_ZERO_VALID     1
#define I_SENS_CAL       0.00533200f  /* ratio per amp - NOMINAL            */
#define I_GAIN_VALID     0
#define I_CAL_CURRENT    0.0f         /* amps the gain was fitted at        */

/* THERMISTOR - FITTED, see NTC_B / NTC_R25 above. A room-temperature point
 * paired with boiling gives 77 C of spread, which settles B perfectly well;
 * what a room-temperature point CANNOT do is settle B on its own.          */

#define ADC_AVG            64     /* RP2040 ADC has DNL artefacts near
                                   * 512/1536/2560/3584 - averaging fixes it
                                   * and buys ~2 effective bits (DESIGN S11) */

/* --------------------------------------------------------- panel settings */
#define LCD_ROTATION   1
#define LCD_W        480
#define LCD_H        320
#define SPI_HZ   12000000UL

#define TOUCH_NATIVE_W  320
#define TOUCH_NATIVE_H  480
/* DETERMINED ON HARDWARE 2026-08-25. Both 0 is correct at LCD_ROTATION = 1.
 *
 * I briefly set both to 1 after reasoning from a verbal description of where
 * the touches were ("top half", "top left corner"), and that was wrong - it
 * mirrored the crosshair. The description was in the VIEWER's frame; the
 * rendered image sits 180 deg from that, so the mapping was already right.
 *
 * THE GROUND TRUTH IS THE CROSSHAIR UNDER THE FINGER, nothing else. Both the
 * touch and the drawing land in the same frame, so if it tracks, it is right.
 * Re-check these two if LCD_ROTATION is ever changed.                       */
#define TOUCH_INVERT_X    0
#define TOUCH_INVERT_Y    0

/* ------------------------------------------------------------ live V/I graph
 * A 5 second window scrolling right to left: oldest at the left edge, newest
 * at the right. Voltage in blue against the LEFT axis, current in red against
 * the RIGHT axis, both on the same time base.
 *
 * The whole plot shifts every frame, so a naive redraw would mean repainting
 * 388x262 px = 203 kB per frame - about 7 fps at 12 MHz, unusably choppy.
 * Instead the renderer remembers the exact pixel span it inked in each column
 * last frame and repaints only that, which is a few hundred pixels per frame
 * rather than a hundred thousand.                                           */
#define GR_X0        46
#define GR_X1       434
#define GR_Y0        30
#define GR_Y1       292
#define GR_W  (GR_X1 - GR_X0)          /* 388 columns                        */
#define GR_H  (GR_Y1 - GR_Y0)          /* 262 rows                           */
#define GR_SECONDS  5.0f
/* MICROseconds, not milliseconds. As ms this truncated 12.887 -> 12 and the
 * window came out 4.66 s wide on an axis labelled 5 s.                      */
#define GR_SAMPLE_US  (uint32_t)(GR_SECONDS * 1000000.0f / GR_W)  /* 12886 us */
#define GR_FRAME_MS   40               /* redraw cap, 25 fps                 */
#define GR_GRIDS      5                /* horizontal gridlines incl. both ends */

#define C_VOLT   0x3BDF                /* bright blue, readable on black     */
#define C_AMP    0xF800                /* red                                */
#define C_GRID   0x2945                /* dark grey                          */

/* ------------------------------------------------------------ RGB565 colours
 * Local on purpose: Arduino_GFX renamed bare BLACK/WHITE to RGB565_BLACK etc,
 * so raw literals are the only spelling that compiles on every version.     */
#define C_BLACK    0x0000
#define C_WHITE    0xFFFF
#define C_RED      0xF800
#define C_GREEN    0x07E0
#define C_BLUE     0x001F
#define C_CYAN     0x07FF
#define C_MAGENTA  0xF81F
#define C_YELLOW   0xFFE0
#define C_ORANGE   0xFD20
#define C_DARKGREY 0x7BEF

/* ------------------------------------------------------- FT6336U registers */
#define FT_ADDR             0x38
#define FT_REG_MODE         0x00
#define FT_REG_TD_STATUS    0x02
#define FT_REG_P1_XH        0x03
#define FT_REG_P1_XL        0x04
#define FT_REG_P1_YH        0x05
#define FT_REG_P1_YL        0x06
#define FT_REG_THGROUP      0x80   /* touch threshold / 16                    */
#define FT_REG_CTRL         0x86   /* monitor-mode ENABLE. 0x01 = allowed (the
                                    * factory default), 0x00 = forbidden.
                                    * THE DEFAULT IS THE PROBLEM: with 0x87 the
                                    * chip drops to a slow scan when idle.    */
#define FT_REG_TIMEENTERMON 0x87   /* idle SECONDS before monitor mode, dflt 30 */
#define FT_REG_PERIODACTIVE 0x88   /* active scan period 0x04..0x14, dflt 0x08,
                                    * LOWER = faster reporting                */
#define FT_REG_PERIODMON    0x89
#define FT_REG_INTMODE      0xA4   /* 0x01 lengthens the INT low time while
                                    * reporting - what level polling and an
                                    * edge ISR both want                      */
#define FT_REG_CIPHER_MID   0x9F   /* expect 0x26 */
#define FT_REG_CIPHER_LOW   0xA0   /* expect 0x01 = FT6336G */
#define FT_REG_CIPHER_HIGH  0xA3   /* expect 0x64 */
#define FT_REG_PMODE        0xA5
#define FT_REG_FIRMID       0xA6
#define FT_REG_VENDOR_ID    0xA8   /* expect 0x11 FocalTech */

/* ------------------------------------------------------------ GFX objects */
Arduino_DataBus *bus = new Arduino_RPiPicoSPI(
    PIN_LCD_RS, PIN_LCD_CS, PIN_SCK, PIN_MOSI, PIN_MISO, spi0);
Arduino_GFX *gfx = new Arduino_ST7796(
    bus, PIN_LCD_RST, LCD_ROTATION, true /* IPS */);

bool gDisplayAttempted = false;
bool gTouchPresent     = false;

/* Touch interrupt. The ISR does NOTHING but set a flag - an I2C transaction
 * inside an ISR would deadlock. The actual register read happens in loop(). */
volatile bool gTouchIrq = false;
static void ctpIsr() { gTouchIrq = true; }
uint32_t gIrqReads = 0, gPollReads = 0;   /* how each sample was triggered */
bool gIrqAttached = false;

/* ------------------------------------------------- runtime calibration state
 * Nominal until 'v' / 'i' fit them. These are RAM only - the sketch prints the
 * constants for you to paste back in. DESIGN.md section 6 wants them in flash;
 * that belongs in the real firmware, not in a diagnostic.                    */
float gVGain      = V_GAIN_CAL;
float gVOffset    = V_OFFSET_CAL;
float gIQuiescent = I_QUIESCENT_CAL;
float gISens      = I_SENS_CAL;
bool  gVCal = (V_CAL_VALID != 0);
bool  gIZero = (I_ZERO_VALID != 0), gICal = (I_GAIN_VALID != 0);
float gICalCurrent = I_CAL_CURRENT;

static float packVolts(uint16_t raw) { return gVGain * (float)raw + gVOffset; }

/* The DESIGN.md 4.2 ratio. Both VCC and VREF cancel out of this, which is the
 * whole point - it does not care what the USB rail is doing.                 */
static float iRatio(uint16_t rawI, uint16_t raw5) {
  if (raw5 < 100) return 0.0f;
  return (float)rawI / (float)raw5 * G2_OVER_G1;
}
static float ampsFrom(uint16_t rawI, uint16_t raw5) {
  return (iRatio(rawI, raw5) - gIQuiescent) / gISens;
}

/* ------------------------------------------------------- button statistics */
struct Button {
  const char *name;
  const char *net;
  uint8_t     pin;
  bool        idleHigh;        /* pull-up: idle should read HIGH */
  bool        last;
  uint32_t    presses;
  uint32_t    pressStart;
  uint32_t    lastDurationMs;
  uint32_t    bounceUs;        /* worst bounce seen on the last edge */
  bool        everPressed;
  bool        stuck;
};
Button gBtn[2] = {
  {"SW1", "BTN1 (MODE)",      PIN_BTN1, true, true, 0, 0, 0, 0, false, false},
  {"SW2", "BTN2 (ZERO/TARE)", PIN_BTN2, true, true, 0, 0, 0, 0, false, false},
};

/* ==========================================================================
 * Output helpers
 * ========================================================================*/
static void rule()   { Serial.println(F("----------------------------------------------------------------")); }
static void banner(const __FlashStringHelper *s) {
  Serial.println(); rule(); Serial.print(F("  ")); Serial.println(s); rule();
}
static void pass(const char *what, const char *detail) {
  Serial.print(F("  [ PASS ]  ")); Serial.print(what);
  if (detail && *detail) { Serial.print(F("   ")); Serial.print(detail); }
  Serial.println();
}
static void fail(const char *what, const char *detail) {
  Serial.print(F("  [ FAIL ]  ")); Serial.print(what);
  if (detail && *detail) { Serial.print(F("   ")); Serial.print(detail); }
  Serial.println();
}
static void info(const char *what, const char *detail) {
  Serial.print(F("  [ ---- ]  ")); Serial.print(what);
  if (detail && *detail) { Serial.print(F("   ")); Serial.print(detail); }
  Serial.println();
}
static void hex2(uint8_t v) { if (v < 0x10) Serial.print('0'); Serial.print(v, HEX); }

/* Swallow the CR/LF that the Serial Monitor appends to a command character.
 * Without this, the line ending arrives as a second "keypress" one iteration
 * later - which instantly cancelled any stream you had just started, and made
 * every interactive prompt read an empty line and abort. */
static void drainLineEnd() {
  uint32_t t0 = millis();
  while (millis() - t0 < 30) {
    if (!Serial.available()) continue;
    int p = Serial.peek();
    if (p != '\r' && p != '\n') return;
    Serial.read();
    t0 = millis();
  }
}

/* ==========================================================================
 * ADC
 * ========================================================================*/
static uint16_t adcAvg(uint8_t pin) {
  uint32_t acc = 0;
  for (uint16_t i = 0; i < ADC_AVG; i++) { acc += analogRead(pin); delayMicroseconds(20); }
  return (uint16_t)(acc / ADC_AVG);
}
static float adcVolts(uint16_t raw) { return (raw / ADC_MAX) * ADC_VREF; }

/* One analog channel, checked against the value it MUST have on USB power
 * with nothing connected.  Bands are deliberately generous - this is a
 * "is it plausibly alive" test, not a calibration.                          */
struct AdcCheck {
  const char *name;
  uint8_t     pin;
  uint16_t    lo, hi;          /* acceptable raw ADC window */
  const char *proves;
  const char *ifBad;
};

static const AdcCheck ADC_CHECKS[] = {
  { "GP29 V5_SENSE", PIN_V5_SENSE, 2700, 3400,
    "R5, R8, C6 and the +5V rail (expect ~3102 = 2.50 V = VBUS/2)",
    "R5 or R8 missing/wrong, C6 shorted, or no +5V from the RP2040-Zero" },
  { "GP26 I_SENSE ", PIN_I_SENSE,   330,  500,
    "ACS770 powered and alive at quiescent, FB1, C1/C2, R1, R2, C3, C4 (expect ~411)",
    "reads ~0: sensor unpowered (check FB1/+5VS) or dead. reads high: R1/R2 wrong" },
  /* Band deliberately WIDE: ~4095 = no NTC fitted (rails to 3V3, correct on the
   * bench), anything mid-scale = an NTC IS connected and reading. BOTH are
   * passes - the engineering-units block below says which. Only a near-zero
   * reading is a fault. v0.3 wrongly failed a working NTC at 25.5 C.        */
  { "GP28 T_SENSE ", PIN_T_SENSE,   200, 4095,
    "+3V3, R10, RV1, R9, C7 - see the Thermistor line below for open vs connected",
    "reads ~0: R10/RV1 open, or T_NODE shorted to ground" },
  /* This one is a SAFETY INTERLOCK, not a health check: it fails whenever pack
   * voltage is present, which is correct for bench work and expected during a
   * voltage calibration. Read the Pack voltage line below for the real value. */
  { "GP27 V_PACK  ", PIN_V_PACK,      0,   60,
    "nothing on the 60 V input, as required for bench work (expect ~0)",
    "PACK VOLTAGE PRESENT. Intended if you are running 'v' - otherwise disconnect it" },
};

static void adcReport(bool verbose) {
  uint16_t raw[4];
  for (uint8_t i = 0; i < 4; i++) raw[i] = adcAvg(ADC_CHECKS[i].pin);

  for (uint8_t i = 0; i < 4; i++) {
    const AdcCheck &c = ADC_CHECKS[i];
    bool ok = (raw[i] >= c.lo && raw[i] <= c.hi);
    Serial.print(ok ? F("  [ PASS ]  ") : F("  [ FAIL ]  "));
    Serial.print(c.name);
    Serial.print(F("  raw ")); Serial.print(raw[i]);
    Serial.print(F("  ")); Serial.print(adcVolts(raw[i]), 4); Serial.print(F(" V"));
    Serial.print(F("   want ")); Serial.print(c.lo);
    Serial.print(F("-")); Serial.println(c.hi);
    if (verbose) {
      Serial.print(ok ? F("            proves: ") : F("            likely: "));
      Serial.println(ok ? c.proves : c.ifBad);
    }
  }

  /* ---- engineering units ---- */
  float v5   = adcVolts(raw[0]) / G2;                         /* actual VBUS */
  float vpk  = packVolts(raw[3]);
  Serial.print(F("\n  +5V rail (from V5_SENSE) : ")); Serial.print(v5, 3);   Serial.println(F(" V"));
  Serial.print(F("  Pack voltage             : ")); Serial.print(vpk, 3);
  Serial.println(gVCal ? F(" V   [CALIBRATED]") : F(" V   [nominal - run 'v']"));

  /* thermistor */
  float x = raw[2] / ADC_MAX;
  if (x > 0.97f) {
    Serial.println(F("  Thermistor               : OPEN - no NTC connected (expected on the bench)"));
  } else if (x < 0.02f) {
    Serial.println(F("  Thermistor               : SHORT - T_NODE at ground"));
  } else {
    float rntc = RV1_OHMS * x / (1.0f - x);
    float t = 1.0f / (1.0f / 298.15f + logf(rntc / NTC_R25) / NTC_B) - 273.15f;
    Serial.print(F("  Thermistor               : ")); Serial.print(rntc / 1000.0f, 2);
    Serial.print(F(" kohm  ->  ")); Serial.print(t, 1); Serial.println(F(" C"));
  }

  /* ---- the DESIGN.md section 4.2 ratiometric recovery, end to end ----
   * Both VCC and VREF cancel out of this, so if the front end is wired
   * correctly it must return ~0 A with no current flowing - regardless of
   * what the USB rail is actually sitting at.  This is the single most
   * valuable number in the whole report: it exercises R1, R2, R5, R8, the
   * ACS770 and both ADC channels simultaneously.                            */
  if (raw[0] > 100) {
    float amps = ampsFrom(raw[1], raw[0]);
    Serial.print(F("\n  RATIOMETRIC CURRENT (DESIGN.md 4.2)"));
    if (gICal)       Serial.println(F("   [ZERO + GAIN calibrated]"));
    else if (gIZero) Serial.println(F("   [ZERO calibrated, GAIN nominal - run 'i']"));
    else             Serial.println(F("   [nominal - run 'i']"));
    Serial.print(F("    ratio = ")); Serial.print(iRatio(raw[1], raw[0]), 6);
    Serial.print(F("    computed current = ")); Serial.print(amps, 3); Serial.println(F(" A"));
    if (fabsf(amps) < 2.0f)
      pass("analog front end", "within +/-2 A of zero - R1/R2/R5/R8 + ACS770 all consistent");
    else if (fabsf(amps) < 10.0f)
      info("analog front end", "offset present - normal before the section 6 tare, but worth a look");
    else
      fail("analog front end", "far from zero - a divider resistor is wrong or the ACS770 is not right");
    Serial.println(F("    (1 LSB on I_SENSE = 0.046 A, so single-digit noise here is expected)"));
  } else {
    fail("ratiometric check", "skipped - V5_SENSE too low to divide by");
  }
}

/* ==========================================================================
 * LIVE VOLTAGE / CURRENT GRAPH
 * ========================================================================*/
float   grV[GR_W], grI[GR_W];          /* ring buffer of samples            */
int16_t grVy0[GR_W], grVy1[GR_W];      /* pixel span inked last frame, volts */
int16_t grIy0[GR_W], grIy1[GR_W];      /* ditto, amps                        */
volatile uint16_t grHead = 0;          /* index of the NEWEST sample         */
volatile bool     grRunning = false;   /* core 1 samples only while this is set */
volatile uint32_t grSampCount = 0;     /* for the achieved-rate readout      */
float   grAchievedHz = 0.0f;
float   grVMax = 10.0f, grAMax = 5.0f;
uint32_t grLastSample = 0, grLastFrame = 0, grShrinkOk = 0;

static const float GR_VSTEPS[] = {5, 10, 20, 30, 40, 60, 70};
static const float GR_ASTEPS[] = {2, 5, 10, 25, 50, 100, 150};

static float grNice(float v, const float *st, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) if (v <= st[i]) return st[i];
  return st[n - 1];
}
static int16_t grRow(float val, float maxv) {              /* value -> pixel */
  if (maxv <= 0) return GR_Y1 - 1;
  float f = val / maxv;
  if (f < 0.0f) f = 0.0f;
  if (f > 1.0f) f = 1.0f;
  int16_t y = GR_Y1 - 2 - (int16_t)(f * (GR_H - 3));
  if (y < GR_Y0) y = GR_Y0;
  if (y > GR_Y1 - 2) y = GR_Y1 - 2;
  return y;
}
static int16_t grGridRow(uint8_t i) {                      /* gridline pixel */
  return GR_Y1 - 1 - (int16_t)((float)i * (GR_H - 2) / (GR_GRIDS - 1));
}
static bool grIsGrid(int16_t y) {
  for (uint8_t i = 0; i < GR_GRIDS; i++) if (grGridRow(i) == y) return true;
  return false;
}

/* Repaint one column span with the background that belongs there - black,
 * except where a gridline runs through it. */
static void grErase(int16_t x, int16_t y0, int16_t y1) {
  if (y0 < GR_Y0) y0 = GR_Y0;
  if (y1 > GR_Y1 - 1) y1 = GR_Y1 - 1;
  for (int16_t y = y0; y <= y1; y++)
    gfx->drawPixel(x, y, grIsGrid(y) ? C_GRID : C_BLACK);
}

/* Axes, labels and gridlines. Redrawn only when a scale changes. */
static void grFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->drawRect(GR_X0 - 1, GR_Y0 - 1, GR_W + 2, GR_H + 2, C_DARKGREY);
  for (uint8_t i = 0; i < GR_GRIDS; i++) {
    int16_t y = grGridRow(i);
    gfx->drawFastHLine(GR_X0, y, GR_W, C_GRID);
    float fv = grVMax * i / (GR_GRIDS - 1);
    float fa = grAMax * i / (GR_GRIDS - 1);
    gfx->setTextSize(1);
    gfx->setTextColor(C_VOLT); gfx->setCursor(4, y - 3);
    gfx->print(fv, (grVMax >= 10) ? 0 : 1);
    gfx->setTextColor(C_AMP);  gfx->setCursor(GR_X1 + 6, y - 3);
    gfx->print(fa, (grAMax >= 10) ? 0 : 1);
  }
  gfx->setTextSize(1);
  gfx->setTextColor(C_VOLT); gfx->setCursor(4, GR_Y0 - 12);       gfx->print(F("VOLTS"));
  gfx->setTextColor(C_AMP);  gfx->setCursor(GR_X1 + 6, GR_Y0 - 12); gfx->print(F("AMPS"));
  gfx->setTextColor(C_DARKGREY);
  gfx->setCursor(GR_X0, GR_Y1 + 6);        gfx->print(F("-5 s"));
  gfx->setCursor(GR_X1 - 22, GR_Y1 + 6);   gfx->print(F("now"));
  for (uint16_t c = 0; c < GR_W; c++) { grVy0[c] = grVy1[c] = grIy0[c] = grIy1[c] = -1; }
}

static void grReadouts(float v, float a) {
  gfx->setTextSize(2);
  gfx->setTextColor(C_VOLT, C_BLACK); gfx->setCursor(GR_X0 + 4, 6);
  gfx->print(v, 2); gfx->print(F(" V   "));
  gfx->setTextColor(C_AMP, C_BLACK);  gfx->setCursor(GR_X0 + 150, 6);
  gfx->print(a, 2); gfx->print(F(" A   "));
  /* Achieved sample rate, measured - not the rate we asked for. If this drifts
   * from the target the time axis is lying and you should know. */
  gfx->setTextSize(1);
  gfx->setTextColor(C_DARKGREY, C_BLACK); gfx->setCursor(GR_X1 - 62, 12);
  gfx->print(grAchievedHz, 1); gfx->print(F(" Hz  "));
}

/* Runs on CORE 1 only. 32-bit aligned float and uint16_t stores are atomic on
 * this part, so core 0 can read the ring while this writes it without tearing
 * - the worst case is reading one sample older than the newest, which is
 * invisible at 78 Hz.
 *
 * The ADC is a single shared peripheral, so ONLY ONE CORE MAY TOUCH IT. Core 0
 * must not call adcAvg() while grRunning is set; graphStop() clears the flag
 * and waits for core 1 to finish the sample in flight before returning.     */
static void grSample() {
  uint16_t rI = adcAvg(PIN_I_SENSE), r5 = adcAvg(PIN_V5_SENSE), rV = adcAvg(PIN_V_PACK);
  uint16_t h = (grHead + 1) % GR_W;
  grV[h] = packVolts(rV);
  grI[h] = ampsFrom(rI, r5);
  grHead = h;                          /* publish LAST, after the data       */
  grSampCount++;
}

static void grDraw() {
  {  /* measure what core 1 actually achieved over the last second */
    static uint32_t lastT = 0, lastN = 0;
    uint32_t now = millis(), n = grSampCount;
    if (lastT && now - lastT >= 1000) {
      grAchievedHz = (float)(n - lastN) * 1000.0f / (float)(now - lastT);
      lastT = now; lastN = n;
    } else if (!lastT) { lastT = now; lastN = n; }
  }
  /* autoscale: expand at once, shrink only after 2 s of headroom */
  float mv = 0, ma = 0;
  for (uint16_t i = 0; i < GR_W; i++) {
    if (grV[i] > mv) mv = grV[i];
    if (grI[i] > ma) ma = grI[i];
  }
  float wantV = grNice(mv * 1.15f, GR_VSTEPS, 7);
  float wantA = grNice(ma * 1.15f, GR_ASTEPS, 7);
  bool rescale = false;
  if (wantV > grVMax || wantA > grAMax) { grVMax = wantV > grVMax ? wantV : grVMax;
                                          grAMax = wantA > grAMax ? wantA : grAMax;
                                          rescale = true; grShrinkOk = millis() + 2000; }
  else if (millis() > grShrinkOk && (wantV < grVMax || wantA < grAMax)) {
    grVMax = wantV; grAMax = wantA; rescale = true; grShrinkOk = millis() + 2000;
  }
  if (rescale) grFrame();

  for (uint16_t c = 0; c < GR_W; c++) {
    uint16_t idx  = (grHead + 1 + c) % GR_W;
    uint16_t prev = (idx + GR_W - 1) % GR_W;
    int16_t x = GR_X0 + c;

    int16_t vy = grRow(grV[idx], grVMax), vp = (c == 0) ? vy : grRow(grV[prev], grVMax);
    int16_t iy = grRow(grI[idx], grAMax), ip = (c == 0) ? iy : grRow(grI[prev], grAMax);
    int16_t vy0 = vy < vp ? vy : vp, vy1 = (vy > vp ? vy : vp) + 1;   /* 2 px thick */
    int16_t iy0 = iy < ip ? iy : ip, iy1 = (iy > ip ? iy : ip) + 1;

    if (grVy0[c] >= 0 && (grVy0[c] != vy0 || grVy1[c] != vy1)) grErase(x, grVy0[c], grVy1[c]);
    if (grIy0[c] >= 0 && (grIy0[c] != iy0 || grIy1[c] != iy1)) grErase(x, grIy0[c], grIy1[c]);
    gfx->drawFastVLine(x, vy0, vy1 - vy0 + 1, C_VOLT);
    gfx->drawFastVLine(x, iy0, iy1 - iy0 + 1, C_AMP);
    grVy0[c] = vy0; grVy1[c] = vy1; grIy0[c] = iy0; grIy1[c] = iy1;
  }
  grReadouts(grV[grHead], grI[grHead]);
}

volatile bool grCore1Busy = false;

/* ---- CORE 1 -------------------------------------------------------------
 * Does nothing but sample, on a fixed micros() cadence, while the graph is
 * running. Core 0 never blocks it, so the rate no longer collapses when a
 * frame takes 31 ms to draw. This is the split DESIGN.md section 11 specifies. */
void setup1() { }
void loop1() {
  static uint32_t next = 0;
  if (!grRunning) { grCore1Busy = false; next = micros(); delay(1); return; }
  if ((int32_t)(micros() - next) < 0) return;
  next += GR_SAMPLE_US;                      /* += , so it CATCHES UP        */
  if ((int32_t)(micros() - next) > (int32_t)GR_SAMPLE_US * 4)
    next = micros() + GR_SAMPLE_US;          /* too far behind - resync      */
  grCore1Busy = true;
  grSample();
  grCore1Busy = false;
}

/* Clear the flag and wait for core 1 to release the ADC before core 0 uses it. */
static void graphStop() {
  grRunning = false;
  uint32_t t0 = millis();
  while (grCore1Busy && millis() - t0 < 50) { }
  delay(5);
}

static void graphStart() {
  banner(F("LIVE V / I GRAPH"));
  if (!gDisplayAttempted) { gfx->begin(SPI_HZ); gDisplayAttempted = true; }
  Serial.print(F("  5 s window, ")); Serial.print(GR_W);
  Serial.print(F(" columns, one sample every ")); Serial.print(GR_SAMPLE_US);
  Serial.print(F(" us = ")); Serial.print(1000000.0f / GR_SAMPLE_US, 1);
  Serial.println(F(" Hz target."));
  Serial.println(F("  Sampling runs on CORE 1, drawing on core 0, so a slow frame no"));
  Serial.println(F("  longer starves the sampler. The achieved rate is shown top-right"));
  Serial.println(F("  on the panel - if it does not match the target, the axis is lying."));
  Serial.println(F("  Blue = volts (left axis). Red = amps (right axis). Axes autoscale."));
  if (!gVCal) info("voltage", "NOT calibrated - run 'v'. Trace is still live and correct in shape.");
  if (!gICal) info("current", "NOT calibrated - run 'i'. Same caveat.");
  Serial.println(F("  Press any key (not just Enter) to stop."));
  for (uint16_t i = 0; i < GR_W; i++) { grV[i] = 0; grI[i] = 0; }
  grHead = 0; grVMax = 10.0f; grAMax = 5.0f; grShrinkOk = millis() + 2000;
  grSampCount = 0; grAchievedHz = 0.0f;
  grFrame();
  grLastFrame = millis();
  grRunning = true;                     /* release core 1 LAST              */
}

/* ==========================================================================
 * Two-point NTC calibration  (DESIGN.md section 6 step 4)
 *
 * A single point can only fit R25. Fitting B as well needs two temperatures,
 * and they should be far apart or the solve is ill-conditioned.
 *
 * The two free fixed points worth using:
 *   ICE BATH   crushed ice + water, well stirred = 0.00 C BY DEFINITION.
 *              More trustworthy than any thermometer you own.
 *   BOILING    ~99.8 C at 50 m elevation. Drops ~1 C per 285 m, so correct
 *              for your altitude rather than assuming 100.0 C.
 *
 * Resistance can come from the board's own ADC (calibrates the WHOLE chain,
 * including any error in RV1_OHMS) or from a DMM (better absolute accuracy,
 * but does not check the board). Enter 0 to use the board.
 * ========================================================================*/
static bool readLine(char *buf, uint8_t len, uint32_t timeoutMs) {
  uint8_t i = 0; uint32_t t0 = millis();
  for (;;) {
    if (millis() - t0 > timeoutMs) return false;
    if (!Serial.available()) continue;
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[i] = 0; return true; }
    if (i < len - 1) buf[i++] = (char)c;
    t0 = millis();                       /* keep the clock alive while typing */
  }
}

static bool promptFloat(const __FlashStringHelper *p, float *out) {
  char b[24];
  Serial.print(F("  ")); Serial.print(p); Serial.print(F(" > "));
  if (!readLine(b, sizeof(b), 180000)) { Serial.println(F("(timeout)")); return false; }
  Serial.println(b);
  if (!b[0]) { Serial.println(F("  (empty - aborted)")); return false; }
  *out = (float)atof(b);
  return true;
}

static bool ntcPoint(uint8_t n, float *tC, float *rOhm) {
  Serial.print(F("\n  ---------- POINT ")); Serial.print(n); Serial.println(F(" ----------"));
  Serial.println(F("  Let the thermistor settle for a minute before entering anything."));
  if (!promptFloat(F("actual temperature, C            "), tC)) return false;
  float r;
  if (!promptFloat(F("measured resistance, ohms (0=ADC)"), &r)) return false;
  if (r <= 0.0f) {
    uint16_t raw = adcAvg(PIN_T_SENSE);
    float x = raw / ADC_MAX;
    if (x <= 0.01f || x >= 0.99f) {
      Serial.print(F("  ADC railed at raw ")); Serial.print(raw);
      Serial.println(F(" - NTC open or shorted. Aborted."));
      return false;
    }
    r = RV1_OHMS * x / (1.0f - x);
    Serial.print(F("  board raw ")); Serial.print(raw);
    Serial.print(F("   ->  R_ntc = ")); Serial.print(r / 1000.0f, 3);
    Serial.println(F(" k   (via RV1_OHMS)"));
  }
  if (r < 100.0f || r > 5000000.0f) {
    Serial.println(F("  Resistance out of any sane range. Aborted."));
    return false;
  }
  *rOhm = r;
  return true;
}

static void ntcCalibrate() {
  banner(F("TWO-POINT NTC CALIBRATION"));
  Serial.println(F("  Point 1: ICE BATH  - crushed ice + water, stirred, is 0.00 C exactly."));
  Serial.println(F("  Point 2: BOILING   - ~99.8 C at 50 m. Correct for YOUR altitude."));
  Serial.println(F("\n  SEAL THE PROBE FIRST. Bare leads in water put a few hundred kohm"));
  Serial.println(F("  across the thermistor, which wrecks the COLD point specifically."));
  Serial.println(F("  A thin plastic bag or a sealed straw is enough. Immerse fully,"));
  Serial.println(F("  stir, and give it 30 s to settle before entering anything."));
  Serial.println(F("  Enter a blank line at any prompt to abort.\n"));
  Serial.println(F("  Type the value then press Enter. Set the Serial Monitor line"));
  Serial.println(F("  ending to Newline or Both NL & CR, or nothing will register."));

  float t1, r1, t2, r2;
  if (!ntcPoint(1, &t1, &r1)) return;
  if (!ntcPoint(2, &t2, &r2)) return;

  if (fabsf(t1 - t2) < 1.0f) {
    fail("calibration", "the two temperatures are the same - cannot solve for B");
    return;
  }
  if (fabsf(t1 - t2) < 20.0f)
    info("warning", "points less than 20 C apart - B will be poorly conditioned");
  if ((t1 < t2 && r1 <= r2) || (t1 > t2 && r1 >= r2)) {
    fail("calibration", "resistance did not fall as temperature rose - not an NTC, or the points are mixed up");
    return;
  }

  float k1 = t1 + 273.15f, k2 = t2 + 273.15f;
  float Bfit   = logf(r1 / r2) / (1.0f / k1 - 1.0f / k2);
  float R25fit = r1 * expf(-Bfit * (1.0f / k1 - 1.0f / 298.15f));

  Serial.println();
  rule();
  Serial.print(F("  point 1:  ")); Serial.print(t1, 2); Serial.print(F(" C  "));
  Serial.print(r1 / 1000.0f, 3); Serial.println(F(" k"));
  Serial.print(F("  point 2:  ")); Serial.print(t2, 2); Serial.print(F(" C  "));
  Serial.print(r2 / 1000.0f, 3); Serial.println(F(" k"));
  rule();
  Serial.print(F("  FITTED  B   = ")); Serial.println(Bfit, 1);
  Serial.print(F("  FITTED  R25 = ")); Serial.print(R25fit, 1);
  Serial.print(F(" ohm  (")); Serial.print(R25fit / 1000.0f, 2); Serial.println(F(" k)"));

  /* PLAUSIBILITY. A real +/-1% part cannot land far from nominal. If the fit
   * says otherwise, the DATA is wrong - almost always the bath, not the part.
   * A beta fit has only 2 free parameters and CANNOT absorb a temperature-
   * dependent measurement error, so a bad point sends R25 and B somewhere
   * arbitrary and a different pair of points sends them somewhere else. */
  bool sane = true;
  if (Bfit < NTC_B * 0.85f || Bfit > NTC_B * 1.15f) {
    fail("B implausible", "more than 15% from nominal - suspect the measurement");
    sane = false;
  }
  if (R25fit < NTC_R25 * 0.75f || R25fit > NTC_R25 * 1.25f) {
    fail("R25 implausible", "more than 25% from nominal - suspect the measurement");
    sane = false;
  }
  if (!sane) {
    Serial.println(F("\n  DO NOT PASTE THESE. Check the measurement first:"));
    Serial.println(F("   1. DMM ACROSS THE THERMISTOR, IN THE BATH, at the same moment."));
    Serial.print  (F("      A healthy part reads about "));
    Serial.print(NTC_R25 / 1000.0f, 0); Serial.print(F("k at 25 C, "));
    Serial.print(NTC_R25 * expf(NTC_B * (1.0f/273.15f - 1.0f/298.15f)) / 1000.0f, 0);
    Serial.print(F("k at 0 C, "));
    Serial.print(NTC_R25 * expf(NTC_B * (1.0f/373.0f - 1.0f/298.15f)) / 1000.0f, 1);
    Serial.println(F("k at 100 C."));
    Serial.println(F("   2. IS THE PROBE WATERPROOF? Bare leads in water shunt the"));
    Serial.println(F("      thermistor. That drags COLD readings down hard (R_ntc is"));
    Serial.println(F("      high there) and barely touches hot ones - which is exactly"));
    Serial.println(F("      the shape that breaks a 2-point fit. Seal it in a bag."));
    Serial.println(F("   3. IS IT FULLY IMMERSED AND STIRRED? Lead conduction pulls a"));
    Serial.println(F("      shallow probe toward room temperature."));
  }
  if (sane) {
    Serial.print(F("\n  vs nominal: B ")); Serial.print((Bfit / NTC_B - 1.0f) * 100.0f, 2);
    Serial.print(F(" %   R25 ")); Serial.print((R25fit / NTC_R25 - 1.0f) * 100.0f, 2);
    Serial.println(F(" %"));
    Serial.println(F("\n  PASTE THESE INTO THE TOP OF THIS FILE AND RE-FLASH:"));
    Serial.println(F("  ------------------------------------------------------"));
    Serial.print(F("  #define NTC_B          ")); Serial.print(Bfit, 1); Serial.println(F("f"));
    Serial.print(F("  #define NTC_R25        ")); Serial.print(R25fit, 1); Serial.println(F("f"));
    Serial.println(F("  ------------------------------------------------------"));

    /* what the fit predicts across the range you actually care about */
    Serial.println(F("\n  Predicted readings with the fitted constants:"));
    Serial.println(F("     true C   R_ntc k    nominal reads   fitted reads"));
    const float TL[] = {0.0f, 25.0f, 40.0f, 60.0f, 80.0f, 100.0f};
    for (uint8_t i = 0; i < 6; i++) {
      float tk = TL[i] + 273.15f;
      float r  = R25fit * expf(Bfit * (1.0f / tk - 1.0f / 298.15f));
      float tn = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25) / NTC_B) - 273.15f;
      float tf = 1.0f / (1.0f / 298.15f + logf(r / R25fit) / Bfit) - 273.15f;
      Serial.print(F("     ")); Serial.print(TL[i], 1);
      Serial.print(F("     ")); Serial.print(r / 1000.0f, 2);
      Serial.print(F("       ")); Serial.print(tn, 2);
      Serial.print(F("          ")); Serial.println(tf, 2);
    }
    Serial.println(F("\n  The 'nominal reads' column is the error you are removing."));
  }
}

/* ==========================================================================
 * VOLTAGE CALIBRATION - two point linear fit  (DESIGN.md section 6 step 2)
 *
 * This channel is NOT ratiometric - it is referenced to GND, so the 3V3 LDO
 * tolerance (+/-2% on a typical ME6211) lands directly on the gain. Section 5
 * budgets +/-2.2% uncalibrated and ~+/-0.3% after this. That is why section 6
 * calls a one-point voltage calibration MANDATORY.
 *
 * Two points rather than one, because there is a real zero offset: with
 * nothing connected the ADC sits around raw 15-30, which is 0.25-0.50 V of
 * apparent pack voltage. One point through the origin cannot remove that.
 * ========================================================================*/
static void voltageCalibrate() {
  banner(F("VOLTAGE CALIBRATION - two point"));
  Serial.println(F("  !! LIVE HIGH VOLTAGE ON THE BOARD !!"));
  Serial.println(F("  The bus copper is DELIBERATELY EXPOSED (no soldermask) so bus bars"));
  Serial.println(F("  can be soldered on. At 57 V that is a live bare conductor. Keep"));
  Serial.println(F("  probes and swarf away from it. DESIGN.md 4.3 also warns that R3 sits"));
  Serial.println(F("  ~1 mm from the LOAD+ pour - a slip there puts 60 V on the ADC node."));
  Serial.println(F("  Feed the supply into the INPUT connector (J1/J2). No load needed."));
  Serial.println(F("  Use a LOW current limit - a few tens of mA is plenty.\n"));
  Serial.println(F("  Use two WIDELY SPACED points. Read the actual voltage from your"));
  Serial.println(F("  meter, not from the supply's own display."));
  Serial.println(F("  The V_PACK safety check in 'a' will report FAIL while voltage is"));
  Serial.println(F("  applied. That is the interlock doing its job, not a fault.\n"));

  float v1, v2; uint16_t r1, r2;

  Serial.println(F("  ---------- POINT 1 (low, e.g. 10-20 V) ----------"));
  if (!promptFloat(F("actual voltage from your meter, V"), &v1)) return;
  r1 = adcAvg(PIN_V_PACK);
  Serial.print(F("  raw = ")); Serial.println(r1);

  Serial.println(F("\n  ---------- POINT 2 (high, e.g. 55-60 V) ----------"));
  if (!promptFloat(F("actual voltage from your meter, V"), &v2)) return;
  r2 = adcAvg(PIN_V_PACK);
  Serial.print(F("  raw = ")); Serial.println(r2);

  if (r1 == r2) { fail("calibration", "both points gave the same raw value"); return; }
  if (fabsf(v2 - v1) < 5.0f)
    info("warning", "points less than 5 V apart - the gain fit will be noisy");

  float gain   = (v2 - v1) / ((float)r2 - (float)r1);
  float offset = v1 - gain * (float)r1;
  float nominal = ADC_VREF / ADC_MAX * VPACK_RATIO;

  if (gain < nominal * 0.5f || gain > nominal * 2.0f) {
    fail("gain out of range", "more than 2x from nominal - check the wiring, not the maths");
    Serial.print(F("    got ")); Serial.print(gain, 6);
    Serial.print(F("  nominal ")); Serial.println(nominal, 6);
    return;
  }

  gVGain = gain; gVOffset = offset; gVCal = true;
  Serial.println();
  rule();
  Serial.print(F("  V_GAIN   = ")); Serial.print(gain, 8);
  Serial.print(F("  V/count   (nominal ")); Serial.print(nominal, 8);
  Serial.print(F(", ")); Serial.print((gain / nominal - 1.0f) * 100.0f, 2); Serial.println(F(" %)"));
  Serial.print(F("  V_OFFSET = ")); Serial.print(offset, 5); Serial.println(F(" V"));
  rule();
  Serial.print(F("  check point 1: ")); Serial.print(packVolts(r1), 3);
  Serial.print(F(" V vs ")); Serial.print(v1, 3);
  Serial.print(F("   err ")); Serial.println(packVolts(r1) - v1, 4);
  Serial.print(F("  check point 2: ")); Serial.print(packVolts(r2), 3);
  Serial.print(F(" V vs ")); Serial.print(v2, 3);
  Serial.print(F("   err ")); Serial.println(packVolts(r2) - v2, 4);
  Serial.println(F("\n  PASTE INTO THE TOP OF THIS FILE:"));
  Serial.print(F("  #define V_GAIN_CAL     ")); Serial.print(gain, 8);  Serial.println(F("f"));
  Serial.print(F("  #define V_OFFSET_CAL   ")); Serial.print(offset, 5); Serial.println(F("f"));
  Serial.println(F("\n  Applied live for this session - run 'a' to see calibrated readings."));
}

/* ==========================================================================
 * CURRENT CALIBRATION - tare then gain  (DESIGN.md section 6 steps 1 and 3)
 *
 * The model is  I = (ratio - quiescent) / sensitivity , where
 *   ratio = (ADC_I / ADC_5V) * (G2/G1)     - both VCC and VREF cancel
 * so the two unknowns map exactly onto the two calibration steps:
 *   TARE at 0 A  -> quiescent   (also removes magnetic remanence)
 *   known current -> sensitivity
 *
 * SECTION 6 SAYS CALIBRATE NEAR THE TOP OF THE RANGE, and it matters more than
 * it looks. Section 5's nonlinearity term is +/-1% of FULL SCALE = +/-1.5 A,
 * FIXED regardless of reading. As a fraction of your calibration current that
 * is the gain uncertainty you inherit:
 *      4 A -> +/-37 %      20 A -> +/-7.5 %
 *     10 A -> +/-15 %      30 A -> +/-5.0 %
 * Use the largest current you can source. This is a property of the sensor,
 * not of the procedure.
 * ========================================================================*/
static void currentCalibrate() {
  banner(F("CURRENT CALIBRATION - tare then gain"));
  Serial.println(F("  SW2 is the tare button in the final firmware, but both switches are"));
  Serial.println(F("  dead on this board, so tare happens here instead.\n"));

  /* ---- step 1: tare ---- */
  Serial.println(F("  ---------- STEP 1: TARE at 0 A ----------"));
  Serial.println(F("  NOTHING in the current path. Disconnect the load AND the supply."));
  Serial.println(F("  Repeat this after any run that saw large current - the ACS770 keeps"));
  Serial.println(F("  up to 400 mA of magnetic offset after a 150 A excursion (section 6)."));
  float dummy;
  if (!promptFloat(F("type 0 and press Enter when the path is clear"), &dummy)) return;

  uint16_t rI = adcAvg(PIN_I_SENSE), r5 = adcAvg(PIN_V5_SENSE);
  if (r5 < 100) { fail("tare", "V5_SENSE too low - cannot form the ratio"); return; }
  float q = iRatio(rI, r5);
  Serial.print(F("  raw I = ")); Serial.print(rI);
  Serial.print(F("   raw 5V = ")); Serial.print(r5);
  Serial.print(F("   ratio = ")); Serial.println(q, 6);
  if (q < 0.05f || q > 0.15f) {
    fail("tare", "quiescent ratio far from the expected 0.1 - is current flowing?");
    return;
  }
  gIQuiescent = q; gIZero = true;
  Serial.print(F("  quiescent = ")); Serial.print(q, 6);
  Serial.print(F("   (nominal 0.100000, ")); Serial.print((q / ACS_QUIESCENT - 1.0f) * 100.0f, 2);
  Serial.println(F(" %)"));

  /* ---- step 2: gain ---- */
  Serial.println(F("\n  ---------- STEP 2: GAIN at a known current ----------"));
  Serial.println(F("  Apply the LARGEST current you can. Read the actual amps from your"));
  Serial.println(F("  supply or clamp meter, then type it and press Enter - the board"));
  Serial.println(F("  samples at the moment you hit Enter, so do it while current flows."));
  Serial.println(F("  A resistive wire load heats and droops, so read it at that instant."));
  float ical;
  if (!promptFloat(F("actual current, A"), &ical)) return;
  if (ical < 1.0f) { fail("gain", "need a meaningful current - 1 A minimum, far more is better"); return; }

  rI = adcAvg(PIN_I_SENSE); r5 = adcAvg(PIN_V5_SENSE);
  float ratio = iRatio(rI, r5);
  Serial.print(F("  raw I = ")); Serial.print(rI);
  Serial.print(F("   raw 5V = ")); Serial.print(r5);
  Serial.print(F("   ratio = ")); Serial.println(ratio, 6);

  float sens = (ratio - gIQuiescent) / ical;
  if (sens < ACS_SENS * 0.5f || sens > ACS_SENS * 2.0f) {
    fail("sensitivity out of range", "more than 2x from nominal - check the current path");
    Serial.print(F("    got ")); Serial.print(sens, 8);
    Serial.print(F("  nominal ")); Serial.println(ACS_SENS, 8);
    return;
  }
  gISens = sens; gICal = true; gICalCurrent = ical;

  Serial.println();
  rule();
  Serial.print(F("  I_QUIESCENT = ")); Serial.print(gIQuiescent, 6); Serial.println();
  Serial.print(F("  I_SENS      = ")); Serial.print(sens, 8);
  Serial.print(F("   (nominal ")); Serial.print(ACS_SENS, 8);
  Serial.print(F(", ")); Serial.print((sens / ACS_SENS - 1.0f) * 100.0f, 2); Serial.println(F(" %)"));
  rule();
  Serial.print(F("  Fitted at ")); Serial.print(ical, 2);
  Serial.print(F(" A, so the nonlinearity term alone gives this gain "));
  Serial.print(F("+/-")); Serial.print(1.5f / ical * 100.0f, 1); Serial.println(F(" % uncertainty."));
  if (ical < 20.0f)
    info("NOTE", "that is a large uncertainty. Recalibrate at a higher current when you can.");

  Serial.println(F("\n  PASTE INTO THE TOP OF THIS FILE:"));
  Serial.print(F("  #define I_QUIESCENT_CAL ")); Serial.print(gIQuiescent, 6); Serial.println(F("f"));
  Serial.print(F("  #define I_SENS_CAL      ")); Serial.print(sens, 8);        Serial.println(F("f"));

  /* ---- step 3: optional verification at a second current ---- */
  Serial.println(F("\n  ---------- STEP 3 (optional): CHECK at another current ----------"));
  Serial.println(F("  Apply a different current and enter it, or blank to skip."));
  float icheck;
  if (!promptFloat(F("actual current, A (blank to skip)"), &icheck)) {
    Serial.println(F("  skipped."));
    return;
  }
  rI = adcAvg(PIN_I_SENSE); r5 = adcAvg(PIN_V5_SENSE);
  float measured = ampsFrom(rI, r5);
  Serial.print(F("  board reads ")); Serial.print(measured, 3);
  Serial.print(F(" A  vs actual ")); Serial.print(icheck, 3);
  Serial.print(F("  ->  error ")); Serial.print(measured - icheck, 3);
  Serial.print(F(" A  (")); Serial.print((measured - icheck) / icheck * 100.0f, 1);
  Serial.println(F(" %)"));
  Serial.println(F("  Section 5 predicts a FIXED +/-1.5 A nonlinearity floor, so a couple"));
  Serial.println(F("  of amps of disagreement here is expected, not a fault."));
}

/* ==========================================================================
 * Buttons
 *
 * Reports far more than up/down.  Bounce duration matters: DESIGN.md removed
 * C12/C13 (the debounce caps) in favour of firmware debounce, so knowing the
 * real bounce figure is what justifies that decision.
 * ========================================================================*/
static void buttonPoll() {
  for (uint8_t i = 0; i < 2; i++) {
    Button &b = gBtn[i];
    bool now = digitalRead(b.pin);
    if (now != b.last) {
      /* measure how long the line keeps flipping after the first edge */
      uint32_t t0 = micros();
      bool s = now, worst = false;
      uint32_t lastFlip = t0;
      while (micros() - t0 < 20000) {           /* watch 20 ms */
        bool v = digitalRead(b.pin);
        if (v != s) { s = v; lastFlip = micros(); worst = true; }
      }
      b.bounceUs = worst ? (lastFlip - t0) : 0;
      bool settled = digitalRead(b.pin);

      if (!settled) {                            /* pressed, active low */
        b.presses++; b.pressStart = millis(); b.everPressed = true;
        Serial.print(F("  >> ")); Serial.print(b.name);
        Serial.print(F(" ("));   Serial.print(b.net);
        Serial.print(F(") PRESSED   #")); Serial.print(b.presses);
        Serial.print(F("   bounce ")); Serial.print(b.bounceUs);
        Serial.println(F(" us"));
      } else {
        b.lastDurationMs = millis() - b.pressStart;
        Serial.print(F("  >> ")); Serial.print(b.name);
        Serial.print(F(" RELEASED  held ")); Serial.print(b.lastDurationMs);
        Serial.print(F(" ms   bounce ")); Serial.print(b.bounceUs);
        Serial.println(F(" us"));
      }
      b.last = settled;
    }
  }
}

static void buttonReport() {
  banner(F("BUTTON REPORT"));
  for (uint8_t i = 0; i < 2; i++) {
    Button &b = gBtn[i];
    bool now = digitalRead(b.pin);
    Serial.print(F("  ")); Serial.print(b.name);
    Serial.print(F("  GP")); Serial.print(b.pin);
    Serial.print(F("  ")); Serial.println(b.net);

    /* idle level - with R15/R16 not fitted this MUST be high via pull-up */
    if (now) pass("    idle level", "HIGH - internal pull-up working, switch open");
    else     fail("    idle level", "LOW while not pressed - switch shorted, or a solder bridge to GND");

    Serial.print(F("    presses seen   : ")); Serial.println(b.presses);
    if (b.everPressed) {
      Serial.print(F("    last held      : ")); Serial.print(b.lastDurationMs); Serial.println(F(" ms"));
      Serial.print(F("    last bounce    : ")); Serial.print(b.bounceUs); Serial.println(F(" us"));
      if (b.bounceUs > 10000)
        info("    bounce", "over 10 ms - firmware debounce will need a longer window");
      else
        pass("    bounce", "within a normal firmware debounce window");
      pass("    wiring", "switch pulls the pin to GND correctly");
    } else {
      info("    status", "NOT YET PRESSED - press it now, then run 'b' again");
    }
    Serial.println();
  }
  Serial.println(F("  SW1 = MODE (cycles display views in the final firmware)"));
  Serial.println(F("  SW2 = ZERO/TARE (DESIGN.md section 6 step 1)"));
}

/* ==========================================================================
 * GPIO bridge / short test
 *
 * Drives one pin low at a time with every other pin held INPUT_PULLUP, then
 * looks for any other pin that follows it down.  That is a solder bridge.
 * Only pull-up current flows, so this is safe.
 *
 * Run it with the RIBBON UNPLUGGED - otherwise the module legitimately drives
 * MISO and CTP_INT and you get false positives.
 * ========================================================================*/
static const uint8_t SHORT_PINS[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const char *SHORT_NAMES[] = {
  "GP0/J13","ESC_SIG","SCK","MOSI","MISO","LCD_CS","LCD_RS","LCD_RST",
  "LCD_LED","SD_CS","CTP_SDA","CTP_SCL","CTP_RST","CTP_INT","BTN1","BTN2" };

static void shortTest() {
  banner(F("GPIO BRIDGE / SHORT TEST"));
  Serial.println(F("  Unplug the FPC ribbon before trusting this - the display module"));
  Serial.println(F("  drives MISO and CTP_INT and will look like a short if connected."));
  Serial.println();

  const uint8_t N = sizeof(SHORT_PINS);
  uint8_t faults = 0;

  for (uint8_t i = 0; i < N; i++) {
    for (uint8_t j = 0; j < N; j++) pinMode(SHORT_PINS[j], INPUT_PULLUP);
    delayMicroseconds(200);
    pinMode(SHORT_PINS[i], OUTPUT);
    digitalWrite(SHORT_PINS[i], LOW);
    delayMicroseconds(200);

    for (uint8_t j = 0; j < N; j++) {
      if (i == j) continue;
      if (digitalRead(SHORT_PINS[j]) == LOW) {
        faults++;
        Serial.print(F("  [ FAIL ]  BRIDGE: ")); Serial.print(SHORT_NAMES[i]);
        Serial.print(F(" (GP")); Serial.print(SHORT_PINS[i]);
        Serial.print(F(")  <-->  "));           Serial.print(SHORT_NAMES[j]);
        Serial.print(F(" (GP")); Serial.print(SHORT_PINS[j]); Serial.println(F(")"));
      }
    }
    pinMode(SHORT_PINS[i], INPUT_PULLUP);
  }

  if (!faults) pass("no bridges between GP0-GP15", "every pin is independent");
  else { Serial.print(F("\n  ")); Serial.print(faults);
         Serial.println(F(" bridge report(s). Note each pair is listed twice.")); }

  /* leave the world as the rest of the sketch expects it */
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_LCD_LED, OUTPUT);
}

/* ==========================================================================
 * GP0 <-> ESC_SIG loopback
 *
 * Optional: jumper J13 (GP0) to J8 pin 1 (ESC signal).  Proves both nets end
 * to end including R17, which nothing else on this board can reach.
 * ========================================================================*/
static void loopbackTest() {
  banner(F("GP0 <-> ESC_SIG LOOPBACK"));
  Serial.println(F("  Requires a jumper from J13 (GP0 pad) to J8 pin 1 (ESC signal)."));
  Serial.println(F("  Without the jumper this will report FAIL - that is expected."));
  Serial.println();

  pinMode(PIN_ESC_SIG, OUTPUT);
  pinMode(PIN_GP0, INPUT_PULLUP);
  bool ok = true;
  for (uint8_t k = 0; k < 4; k++) {
    bool level = k & 1;
    digitalWrite(PIN_ESC_SIG, level);
    delay(2);
    if (digitalRead(PIN_GP0) != level) ok = false;
  }
  if (ok) pass("GP1 -> GP0 loopback", "ESC_SIG and GP0/J13 both continuous (R17 included)");
  else    fail("GP1 -> GP0 loopback", "no jumper fitted, or one of the two nets is open");

  pinMode(PIN_ESC_SIG, INPUT);
  pinMode(PIN_GP0, INPUT_PULLUP);
}

/* ==========================================================================
 * ESC signal output
 * ========================================================================*/
static void escPulseTest() {
  banner(F("ESC SIGNAL OUTPUT"));
  Serial.println(F("  Emitting 50 Hz servo pulses on GP1 -> J8 pin 1 for 5 seconds:"));
  Serial.println(F("  1000 us (idle) for 2 s, then 1500 us for 2 s."));
  Serial.println(F("  Scope J8 pin 1, or plug in a servo. NOTE J8 pin 2 is deliberately"));
  Serial.println(F("  not connected - do not expect a BEC feed there (DESIGN.md section 3)."));
  pinMode(PIN_ESC_SIG, OUTPUT);
  for (uint16_t us = 1000; us <= 1500; us += 500) {
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
      digitalWrite(PIN_ESC_SIG, HIGH); delayMicroseconds(us);
      digitalWrite(PIN_ESC_SIG, LOW);  delayMicroseconds(20000 - us);
    }
    Serial.print(F("    sent ")); Serial.print(us); Serial.println(F(" us"));
  }
  digitalWrite(PIN_ESC_SIG, LOW);
  info("ESC output", "cannot self-verify - confirm with a scope or a servo");
}

/* ==========================================================================
 * Backlight
 * ========================================================================*/
static void backlightTest() {
  banner(F("BACKLIGHT  (GP8 -> R14 -> FPC 8, no SPI involved)"));
  pinMode(PIN_LCD_LED, OUTPUT);
  Serial.println(F("  Ramping 0 -> 255 over ~2 s. Watch the panel edge."));
  analogWrite(PIN_LCD_LED, 0); delay(400);
  for (int d = 0; d <= 255; d += 3) { analogWrite(PIN_LCD_LED, d); delay(6); }
  /* End SOLID ON, not analogWrite(255). The ramp is only a diagnostic; the
   * operating state is a hard-driven pin with NO switching at all. See the
   * note at PIN_LCD_LED for why PWM here is actively harmful. */
  pinMode(PIN_LCD_LED, OUTPUT);
  digitalWrite(PIN_LCD_LED, HIGH);
  info("backlight", "cannot be read back - if it stayed dark the fault is one of:");
  Serial.println(F("      JP1 not bridged / cold joint | R14 missing | FPC 8 open"));
  Serial.println(F("      ribbon reversed or mis-seated | module itself dead"));
  Serial.println(F("    Isolate it: 3 wires to the module's 2.54 mm header,"));
  Serial.println(F("    VCC->5V, GND->GND, LED->5V. Backlight beads should light with"));
  Serial.println(F("    no MCU involved at all (module spec FAQ Q1)."));
}

/* ==========================================================================
 * Bit-banged ST7796U ID read - the DESIGN.md section 11 test
 * ========================================================================*/
static inline void bbDelay() { delayMicroseconds(2); }
static void bbWriteByte(uint8_t b) {
  for (int8_t i = 7; i >= 0; i--) {
    digitalWrite(PIN_MOSI, (b >> i) & 1); bbDelay();
    digitalWrite(PIN_SCK, HIGH); bbDelay();
    digitalWrite(PIN_SCK, LOW);
  }
}
static uint8_t bbReadByte() {
  uint8_t v = 0;
  for (int8_t i = 7; i >= 0; i--) {
    digitalWrite(PIN_SCK, HIGH); bbDelay();
    if (digitalRead(PIN_MISO)) v |= (1 << i);
    digitalWrite(PIN_SCK, LOW);  bbDelay();
  }
  return v;
}
static void bbReadRegister(uint8_t cmd, uint8_t *out, uint8_t len) {
  digitalWrite(PIN_LCD_CS, LOW);
  digitalWrite(PIN_LCD_RS, LOW);  bbWriteByte(cmd);
  digitalWrite(PIN_LCD_RS, HIGH);
  pinMode(PIN_MISO, INPUT_PULLUP);
  digitalWrite(PIN_SCK, HIGH); bbDelay(); digitalWrite(PIN_SCK, LOW); bbDelay();
  for (uint8_t i = 0; i < len; i++) out[i] = bbReadByte();
  digitalWrite(PIN_LCD_CS, HIGH);
}

static void misoTest() {
  banner(F("MISO READ-BACK  (DESIGN.md section 11)"));
  Serial.println(F("  NOTE: a working display does NOT require MISO. The panel is driven"));
  Serial.println(F("  write-only, so this test can fail on a completely healthy board."));
  Serial.println(F("  It is only meaningful with a microSD card actually in the slot."));
  uint8_t d3[4], id4[4];

  pinMode(PIN_LCD_CS,  OUTPUT); digitalWrite(PIN_LCD_CS,  HIGH);
  pinMode(PIN_LCD_RS,  OUTPUT); digitalWrite(PIN_LCD_RS,  HIGH);
  pinMode(PIN_LCD_RST, OUTPUT);
  pinMode(PIN_SCK,     OUTPUT); digitalWrite(PIN_SCK,     LOW);
  pinMode(PIN_MOSI,    OUTPUT); digitalWrite(PIN_MOSI,    LOW);
  pinMode(PIN_SD_CS,   OUTPUT); digitalWrite(PIN_SD_CS,   HIGH);
  pinMode(PIN_MISO,    INPUT_PULLUP);

  digitalWrite(PIN_LCD_RST, HIGH); delay(20);
  digitalWrite(PIN_LCD_RST, LOW);  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH); delay(150);

  bbReadRegister(0xD3, d3, 4);
  bbReadRegister(0x04, id4, 4);
  Serial.print(F("  0xD3 RDID4 :"));
  for (uint8_t i = 0; i < 4; i++) { Serial.print(' '); hex2(d3[i]); } Serial.println();
  Serial.print(F("  0x04 RDDID :"));
  for (uint8_t i = 0; i < 4; i++) { Serial.print(' '); hex2(id4[i]); } Serial.println();

  bool found = false;
  for (uint8_t i = 0; i + 1 < 4; i++) if (d3[i] == 0x77 && d3[i+1] == 0x96) found = true;
  bool allFF = (d3[0]==0xFF && d3[1]==0xFF && d3[2]==0xFF && d3[3]==0xFF);
  bool all00 = (d3[0]==0x00 && d3[1]==0x00 && d3[2]==0x00 && d3[3]==0x00);

  if (found) {
    pass("FPC 9 / SPI_MISO / R13",
         "0x7796 returned - MISO continuous AND the microSD is tri-stating. Section 11 answered, R13 stays 0R.");
  } else if (allFF) {
    info("FPC 9 / SPI_MISO / R13",
         "all 0xFF = nothing is driving the pin. NOT necessarily a fault:");
    Serial.println(F("      * IS THERE A microSD CARD IN THE SLOT? On many of these modules"));
    Serial.println(F("        the FPC MISO pin is driven ONLY by the card. Empty slot = floating"));
    Serial.println(F("        MISO = 0xFF, with a perfectly healthy board and a working panel."));
    Serial.println(F("      * The ST7796 may not drive SDO at all through the module's level"));
    Serial.println(F("        shifter, which is usually one-way for the MCU->panel direction."));
    Serial.println(F("      * Only if the DISPLAY IS ALSO DEAD does this point at the wiring:"));
    Serial.println(F("        module unpowered (JP1) | R13 missing | FPC 9 open | ribbon reversed"));
    Serial.println(F("      Insert a card and re-run 'm'. DESIGN.md S11 asks whether the SD"));
    Serial.println(F("      TRI-STATES cleanly - a question with no meaning on an empty slot."));
  } else if (all00) {
    fail("FPC 9 / SPI_MISO / R13", "all 0x00 = MISO held low. Suspect a short to GND.");
  } else {
    info("FPC 9 / SPI_MISO / R13",
         "unexpected bytes - INCONCLUSIVE, not proof of a fault. Some level-shifted modules need the panel initialised before reads work.");
  }
}

/* ==========================================================================
 * Touch
 * ========================================================================*/
static bool ftRead(uint8_t reg, uint8_t *val) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)FT_ADDR, (uint8_t)1) != 1) return false;
  *val = Wire1.read(); return true;
}
static bool ftReadBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)FT_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}

static void ftWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(FT_ADDR); Wire1.write(reg); Wire1.write(val);
  Wire1.endTransmission();
}

static void touchTest() {
  banner(F("TOUCH CONTROLLER  (I2C1: GP10 SDA / GP11 SCL)"));
  pinMode(PIN_CTP_INT, INPUT_PULLUP);
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, HIGH); delay(10);
  digitalWrite(PIN_CTP_RST, LOW);  delay(20);
  digitalWrite(PIN_CTP_RST, HIGH); delay(300);

  Wire1.setSDA(PIN_CTP_SDA); Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin(); Wire1.setClock(100000); delay(50);

  uint8_t count = 0; gTouchPresent = false;
  Serial.print(F("  I2C1 scan:"));
  for (uint8_t a = 1; a < 127; a++) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() == 0) {
      Serial.print(F(" 0x")); hex2(a); count++;
      if (a == FT_ADDR) gTouchPresent = true;
    }
  }
  if (!count) Serial.print(F(" no devices"));
  Serial.println();

  if (!gTouchPresent) {
    fail("FT6336U at 0x38", "no ACK.");
    Serial.println(F("      NOTE: CTP_SDA/CTP_SCL have NO series resistors on this board -"));
    Serial.println(F("      they go straight from GP10/GP11 to FPC 12/10. So this cannot be"));
    Serial.println(F("      a missing 0R. It is power, the ribbon, or the module."));
    return;
  }
  pass("FT6336U at 0x38", "device is answering");

  uint8_t hi=0, mid=0, low=0, ven=0, fw=0;
  ftRead(FT_REG_CIPHER_HIGH, &hi); ftRead(FT_REG_CIPHER_MID, &mid);
  ftRead(FT_REG_CIPHER_LOW,  &low); ftRead(FT_REG_VENDOR_ID, &ven);
  ftRead(FT_REG_FIRMID,      &fw);
  Serial.print(F("  chip 0xA3/0x9F/0xA0 = ")); hex2(hi); Serial.print('/');
  hex2(mid); Serial.print('/'); hex2(low);
  Serial.print(F("   vendor 0xA8 = ")); hex2(ven);
  Serial.print(F("   fw 0xA6 = ")); hex2(fw); Serial.println();
  if (hi == 0x64 && ven == 0x11) pass("FT6336U identity", "0x64 + FocalTech 0x11 as expected");
  else                           fail("FT6336U identity", "expected 0xA3=0x64 and 0xA8=0x11");

  /* ---- configure for responsiveness ------------------------------------
   * The factory defaults are tuned for battery life, not for a bench meter,
   * and they are why touch felt hit-or-miss: 0x86 allows MONITOR mode and
   * 0x87 drops into it after 30 SECONDS of no touch, where the panel scans
   * slowly and the first touch back is late or lost entirely.              */
  ftWrite(FT_REG_MODE,         0x00);  /* normal working mode                */
  ftWrite(FT_REG_PMODE,        0x00);  /* P_ACTIVE                           */
  ftWrite(FT_REG_CTRL,         0x00);  /* FORBID monitor mode - the main fix */
  ftWrite(FT_REG_PERIODACTIVE, 0x04);  /* fastest allowed scan period        */
  ftWrite(FT_REG_INTMODE,      0x01);  /* hold INT low while reporting       */
  Wire1.setClock(400000);              /* 100k was a bring-up choice; the
                                        * FT6336U is happy at 400k and it
                                        * cuts each read to a quarter        */
  delay(5);

  uint8_t rb;
  Serial.print(F("  config read-back:"));
  if (ftRead(FT_REG_CTRL, &rb))         { Serial.print(F("  0x86=")); hex2(rb); }
  if (ftRead(FT_REG_PERIODACTIVE, &rb)) { Serial.print(F("  0x88=")); hex2(rb); }
  if (ftRead(FT_REG_INTMODE, &rb))      { Serial.print(F("  0xA4=")); hex2(rb); }
  if (ftRead(FT_REG_PMODE, &rb))        { Serial.print(F("  0xA5=")); hex2(rb); }
  Serial.println(F("   (want 0x86=00, 0x88=04)"));

  /* ---- interrupt ---- */
  if (!gIrqAttached) {
    attachInterrupt(digitalPinToInterrupt(PIN_CTP_INT), ctpIsr, FALLING);
    gIrqAttached = true;
  }
  gIrqReads = gPollReads = 0;
  Serial.print(F("  CTP_INT (GP13) idle level: "));
  Serial.println(digitalRead(PIN_CTP_INT) ? F("HIGH - correct, asserts LOW on touch")
                                          : F("LOW while untouched - suspect"));
}

/* Crosshair sized to be visible AROUND a fingertip rather than hidden under
 * it: long arms with a gap in the middle, plus a ring wider than a typical
 * contact patch. 2 px everywhere so it reads clearly on video.             */
#define CROSS_ARM   34
#define CROSS_GAP    9
#define CROSS_RING  26
static void drawCross(int16_t x, int16_t y, uint16_t c) {
  int16_t len = CROSS_ARM - CROSS_GAP;
  for (int8_t k = 0; k < 2; k++) {
    gfx->drawFastHLine(x - CROSS_ARM, y + k, len, c);
    gfx->drawFastHLine(x + CROSS_GAP, y + k, len, c);
    gfx->drawFastVLine(x + k, y - CROSS_ARM, len, c);
    gfx->drawFastVLine(x + k, y + CROSS_GAP, len, c);
  }
  gfx->drawCircle(x, y, CROSS_RING,     c);
  gfx->drawCircle(x, y, CROSS_RING - 1, c);
}

static bool touchRead(uint16_t *rx, uint16_t *ry) {
  uint8_t b[5];
  if (!ftReadBlock(FT_REG_TD_STATUS, b, 5)) return false;
  uint8_t n = b[0] & 0x0F;
  if (n == 0 || n > 2) return false;
  *rx = (uint16_t)(b[1] & 0x0F) << 8 | b[2];
  *ry = (uint16_t)(b[3] & 0x0F) << 8 | b[4];
  return true;
}
static void touchMap(uint16_t rx, uint16_t ry, int16_t *sx, int16_t *sy) {
  int16_t x, y;
#if (LCD_ROTATION == 1)
  x = (int16_t)ry;  y = (int16_t)(TOUCH_NATIVE_W - 1 - rx);
#elif (LCD_ROTATION == 3)
  x = (int16_t)(TOUCH_NATIVE_H - 1 - ry); y = (int16_t)rx;
#else
  x = (int16_t)rx;  y = (int16_t)ry;
#endif
#if TOUCH_INVERT_X
  x = LCD_W - 1 - x;
#endif
#if TOUCH_INVERT_Y
  y = LCD_H - 1 - y;
#endif
  *sx = x; *sy = y;
}

/* ==========================================================================
 * Display
 * ========================================================================*/
static void displayTest() {
  banner(F("DISPLAY  (ST7796U over SPI0)"));
  gfx->begin(SPI_HZ);
  gDisplayAttempted = true;
  /* Arduino_GFX's begin() on an SPI bus returns true unconditionally - it does
   * not read anything back from the panel.  It would return true with the
   * ribbon unplugged, so its result is NOT reported as a pass here.          */
  info("gfx->begin()", "called. This proves NOTHING about the panel - the library");
  Serial.println(F("            does not read back. Only your eyes can pass this test."));
  Serial.println();

  const uint16_t fills[] = {C_RED, C_GREEN, C_BLUE, C_WHITE, C_BLACK};
  const char    *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  for (uint8_t i = 0; i < 5; i++) {
    Serial.print(F("    fill ")); Serial.println(names[i]);
    gfx->fillScreen(fills[i]); delay(400);
  }
  const uint16_t bars[] = {C_RED,C_GREEN,C_BLUE,C_YELLOW,C_MAGENTA,C_CYAN,C_WHITE,C_BLACK};
  int16_t bw = LCD_W / 8;
  for (uint8_t i = 0; i < 8; i++) gfx->fillRect(i*bw, 0, bw, LCD_H, bars[i]);
  Serial.println(F("    8 colour bars"));
  delay(1200);

  gfx->fillScreen(C_BLACK);
  gfx->fillRect(0, 0, 24, 24, C_RED);
  gfx->fillRect(LCD_W-24, 0, 24, 24, C_GREEN);
  gfx->fillRect(0, LCD_H-24, 24, 24, C_BLUE);
  gfx->fillRect(LCD_W-24, LCD_H-24, 24, 24, C_YELLOW);
  gfx->drawRect(0, 0, LCD_W, LCD_H, C_WHITE);
  gfx->drawLine(0, 0, LCD_W-1, LCD_H-1, C_WHITE);
  gfx->drawLine(LCD_W-1, 0, 0, LCD_H-1, C_WHITE);
  Serial.println(F("    corner markers + border + diagonals"));
  Serial.println(F("\n  Did you see 5 fills, 8 bars, then 4 corner squares?"));
  Serial.println(F("    yes -> the SPI path works, panel is good"));
  Serial.println(F("    backlight on but black -> R11 (SCK) / R12 (MOSI) / FPC 3,4,5"));
  Serial.println(F("    nothing at all -> power, see the BACKLIGHT section"));
}

/* ==========================================================================
 * Summary
 * ========================================================================*/
static void summary() {
  banner(F("SUMMARY - what is proven"));
  Serial.println(F("  Proven WITHOUT the display (board-only):"));
  Serial.println(F("    ADC channels, analog front end, ACS770, buttons, GPIO bridges."));
  Serial.println(F("    These are independent of the FPC ribbon entirely."));
  Serial.println();
  Serial.println(F("  Needs the display attached and powered:"));
  Serial.println(F("    backlight, SPI panel output, MISO read-back, I2C touch."));
  Serial.println();
  Serial.println(F("  If every display test fails but the board tests pass, the fault is"));
  Serial.println(F("  in JP1 / the ribbon / the module - NOT in the RP2040 or its wiring."));
}

static void help() {
  banner(F("COMMANDS  (type a letter, press Enter)"));
  Serial.println(F("    a   analog / ADC report, with expected values"));
  Serial.println(F("    A   analog report streaming, 2 Hz (any key stops)"));
  Serial.println(F("    b   button report"));
  Serial.println(F("    s   GPIO bridge / short test (unplug the ribbon first)"));
  Serial.println(F("    k   backlight ramp"));
  Serial.println(F("    m   MISO read-back test (DESIGN.md section 11)"));
  Serial.println(F("    d   display test pattern"));
  Serial.println(F("    t   touch controller scan + identity"));
  Serial.println(F("    x   touch coordinate stream (any key stops)"));
  Serial.println(F("    e   ESC signal output on GP1"));
  Serial.println(F("    l   GP0 <-> ESC_SIG loopback (needs a jumper)"));
  Serial.println(F("    n   two-point NTC calibration (ice bath + boiling)"));
  Serial.println(F("    v   pack-voltage calibration, two point"));
  Serial.println(F("    i   current calibration, tare then gain"));
  Serial.println(F("    g   LIVE V/I GRAPH - 5 s scrolling window"));
  Serial.println(F("    c   show the stored calibration constants"));
  Serial.println(F("    r   re-run the full sequence"));
  Serial.println(F("    ?   this list"));
  Serial.println(F("\n  Buttons are logged live at all times - just press them."));
}

static void calShow() {
  Serial.println();
  Serial.println(F("  ---- stored calibration (baked into this build) ----"));
  Serial.print(F("   VOLTAGE  ")); Serial.print(gVCal ? F("CALIBRATED  ") : F("nominal     "));
  Serial.print(F("gain ")); Serial.print(gVGain, 8);
  Serial.print(F("  offset ")); Serial.print(gVOffset, 5); Serial.println(F(" V"));
  Serial.print(F("   CURRENT  "));
  Serial.print(gICal ? F("ZERO+GAIN   ") : (gIZero ? F("ZERO only   ") : F("nominal     ")));
  Serial.print(F("quiescent ")); Serial.print(gIQuiescent, 6);
  Serial.print(F("  sens ")); Serial.print(gISens, 8);
  if (gICal && gICalCurrent > 0) {
    Serial.print(F("  fitted at ")); Serial.print(gICalCurrent, 1);
    Serial.print(F(" A  (+/-")); Serial.print(1.5f / gICalCurrent * 100.0f, 1);
    Serial.print(F("% from nonlinearity)"));
  }
  Serial.println();
  Serial.print(F("   THERM    RV1 ")); Serial.print(RV1_OHMS / 1000.0f, 2);
  Serial.print(F("k   R25 ")); Serial.print(NTC_R25 / 1000.0f, 2);
  Serial.print(F("k   B ")); Serial.print(NTC_B, 1);
  Serial.println(F("   [FITTED 2026-08-25, B from 21.1/100 C, R25 from 22.1 C dry]"));
  if (!gICal) Serial.println(F("   -> current GAIN is nominal. Run 'i' and paste I_SENS_CAL."));
  Serial.println();
}

static void fullRun() {
  banner(F("RC LOGGING CURRENT METER - Rev A BOARD DIAGNOSTIC"));
  Serial.print(F("  firmware v")); Serial.print(F(FW_VERSION));
  Serial.print(F("   updated ")); Serial.println(F(FW_UPDATED));
  Serial.print(F("  BUILT ")); Serial.print(F(FW_BUILT));
  Serial.println(F("   <- if this is not the last time you compiled, the upload did not take"));
  Serial.println(F("  USB POWER ONLY. Nothing in the 60 V current path."));
  Serial.println(F("  Board-only tests run FIRST and do not depend on the display."));
  calShow();

  banner(F("ANALOG / ADC  (board only - no ribbon needed)"));
  adcReport(true);

  banner(F("BUTTONS  (board only)"));
  Serial.println(F("  Press SW1 and SW2 now - events are logged live."));
  Serial.println(F("  Then type 'b' for the full report."));
  buttonReport();

  backlightTest();
  misoTest();
  displayTest();
  touchTest();
  summary();
  help();
}

/* ==========================================================================
 * setup / loop
 * ========================================================================*/
void setup() {
  /* Section 11: the microSD must never be selected. First GPIO touched. */
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_LCD_LED, OUTPUT); digitalWrite(PIN_LCD_LED, LOW);

  analogReadResolution(12);

#if USE_NEOPIXEL
  px.begin(); px.setPixelColor(0, px.Color(8, 0, 0)); px.show();
#endif

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) delay(10);

  gBtn[0].last = digitalRead(PIN_BTN1);
  gBtn[1].last = digitalRead(PIN_BTN2);

  /* safety first: shout if pack voltage is present */
  uint16_t vp = adcAvg(PIN_V_PACK);
  if (vp > 60) {
    Serial.println();
    Serial.println(F("  ****************************************************************"));
    Serial.print  (F("  ** WARNING: V_PACK reads "));
    Serial.print(adcVolts(vp) * VPACK_RATIO, 1);
    Serial.println(F(" V - PACK VOLTAGE PRESENT      **"));
    Serial.println(F("  ** Disconnect the battery before continuing bench work.       **"));
    Serial.println(F("  ****************************************************************"));
  }

#if USE_NEOPIXEL
  px.setPixelColor(0, px.Color(0, 8, 0)); px.show();
#endif

  fullRun();
}

void loop() {
  static uint32_t nextStream = 0;
  static bool streamAdc = false, streamTouch = false;
  static int16_t px_ = -1, py_ = -1;

  buttonPoll();

  if (Serial.available()) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      /* stray line ending - NOT a keypress. Ignoring this is what lets a
       * stream survive the Enter that started it. */
    }
    else if (streamAdc || streamTouch || grRunning) {
      if (grRunning) graphStop();       /* let core 1 release the ADC first  */
      streamAdc = streamTouch = false; drainLineEnd();
      Serial.println(F("  (stream stopped)"));
    }
    else { drainLineEnd(); switch (c) {
      case 'a': banner(F("ANALOG / ADC")); adcReport(true);  break;
      case 'A': streamAdc = true; Serial.println(F("  streaming ADC, any key stops")); break;
      case 'b': buttonReport();   break;
      case 's': shortTest();      break;
      case 'k': backlightTest();  break;
      case 'm': misoTest();       break;
      case 'd': displayTest();    break;
      case 't': touchTest();      break;
      case 'x':
        if (!gTouchPresent) {
          info("touch not detected yet", "running the scan first");
          touchTest();
        }
        if (!gTouchPresent) {
          fail("touch stream", "no FT6336U at 0x38 - nothing to stream");
        } else {
          streamTouch = true;
          if (gDisplayAttempted) {          /* clean slate so black erase works */
            gfx->fillScreen(C_BLACK);
            gfx->drawRect(0, 0, LCD_W, LCD_H, C_DARKGREY);
            gfx->setTextSize(1); gfx->setTextColor(C_DARKGREY);
            gfx->setCursor(6, 6); gfx->print(F("touch test - drag a finger"));
          }
          Serial.println(F("  Streaming touch. Drag a finger; the crosshair should follow it."));
          Serial.println(F("  Press any key (not just Enter) to stop."));
        }
        break;
      case 'e': escPulseTest();   break;
      case 'l': loopbackTest();   break;
      case 'n': ntcCalibrate();   break;
      case 'v': voltageCalibrate(); break;
      case 'i': currentCalibrate(); break;
      case 'g': graphStart();     break;
      case 'r': fullRun();        break;
      case 'c': calShow();        break;
      case '?': help();           break;
      default: break;
    } }
  }

  if (streamAdc && millis() >= nextStream) {
    nextStream = millis() + 500;
    adcReport(false);
  }

  if (streamTouch && gTouchPresent) {
    static uint32_t lastPoll = 0, lastStat = 0;
    /* INT asserted (edge caught by the ISR, or still held low) means a report
     * is waiting. Otherwise fall back to a slow timed poll, so a broken
     * CTP_INT degrades to the old behaviour instead of looking like dead
     * touch - and the counters below make that visible.                     */
    bool byIrq   = gTouchIrq || (digitalRead(PIN_CTP_INT) == LOW);
    bool byTimer = (millis() - lastPoll) >= 50;
    if (byIrq || byTimer) {
      gTouchIrq = false; lastPoll = millis();
      if (byIrq) gIrqReads++; else gPollReads++;

    uint16_t rx, ry;
    if (touchRead(&rx, &ry)) {
      int16_t sx, sy; touchMap(rx, ry, &sx, &sy);
      if (millis() >= nextStream) {
        nextStream = millis() + 100;
        Serial.print(F("  touch raw x=")); Serial.print(rx);
        Serial.print(F(" y=")); Serial.print(ry);
        Serial.print(F("   mapped x=")); Serial.print(sx);
        Serial.print(F(" y=")); Serial.println(sy);
      }
      if (gDisplayAttempted && sx >= 0 && sx < LCD_W && sy >= 0 && sy < LCD_H) {
        /* Erasing with black is only correct because 'x' clears the screen
         * first. Drawing black over the test pattern is what was punching
         * permanent holes in the white diagonals. */
        if (px_ >= 0) drawCross(px_, py_, C_BLACK);
        drawCross(sx, sy, C_GREEN);
        px_ = sx; py_ = sy;
      }
    }
    }  /* byIrq || byTimer */

    /* once a second, say how the samples were being triggered */
    if (millis() - lastStat > 1000) {
      lastStat = millis();
      if (gIrqReads + gPollReads > 0) {
        Serial.print(F("  [rate] INT-triggered ")); Serial.print(gIrqReads);
        Serial.print(F("   timed-fallback ")); Serial.print(gPollReads);
        if (gIrqReads == 0)
          Serial.print(F("   <- NO INT ACTIVITY: check CTP_INT / FPC 13 / GP13"));
        Serial.println();
        gIrqReads = gPollReads = 0;
      }
    }
  }

  if (grRunning) {                      /* core 0 draws only; core 1 samples */
    uint32_t now = millis();
    if (now - grLastFrame >= GR_FRAME_MS) { grLastFrame = now; grDraw(); }
  }

  delay((streamTouch || grRunning) ? 0 : 2);
}
