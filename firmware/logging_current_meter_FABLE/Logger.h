/* ==========================================================================
 * Logger.h — SD-card CSV logging and the USB CSV stream. Core 0 only.
 *
 * DATA PATH. Core 1 pushes one LogRec per 13.158 ms tick into gLogRing
 * (Sampler.h). logTick() drains it every loop pass, whether or not anything
 * is recording — so the ring never fills, the current trigger sees every
 * sample, and starting a log is instant. Both outputs decimate the same
 * record stream by gSet.logRateIdx.
 *
 * SD. SdFat on SPI0, shared with the display. Both run on core 0 and every
 * access is inside its own SPI transaction (the TFT at 40 MHz, the card at
 * LOG_SD_MHZ), so they cannot interleave mid-transfer. SdFat is started with
 * USER_SPI_BEGIN so it does not re-run SPI.begin() under the display.
 *  - Files are LOG0001.CSV upward; no RTC, so the index is the ordering.
 *  - NOT pre-allocated. SdFat's preAllocate() sets the file size to the whole
 *    reservation immediately, so a power pull would leave megabytes of junk
 *    after the data. Appending costs a FAT update per cluster — trivial at
 *    ~8 KB/s.
 *  - Written in 512 B blocks from a line buffer, synced every LOG_SYNC_MS so
 *    a power pull normally costs only the last couple of seconds.
 *  - Starting a log also resets the energy counters and run timer (the same
 *    action as Live View's energy reset), so the screen and the file agree.
 *
 * TRIGGERS (gSet.logMode). START/STOP on the LOG screen and the serial 'g'
 * key work in every mode.
 *  - MANUAL   start/stop by hand only.
 *  - CYCLE    Test Mode START CYCLE opens a log; STOP CYCLE closes it.
 *  - CURRENT  a new log opens when raw current exceeds gSet.logThreshA for
 *             LOG_TRIG_TICKS consecutive ticks, and includes the preceding
 *             LOG_PRETRIG_TICKS of samples (negative t_ms) so the spin-up is
 *             not lost. Once the log ends the trigger re-arms only after the
 *             current has stayed below threshold for LOG_REARM_MS, so a run
 *             that outlasts its duration does not immediately start another.
 * DURATION (gSet.logDurIdx) ends any log after 1-15 min of data; "until
 * stopped" means manual/cycle stop, or in CURRENT mode LOG_IDLE_STOP_MS
 * below threshold.
 *
 * USB STREAM. The same rows minus the energy columns, printed to Serial when
 * enabled, with every non-data line prefixed '#'. Output is guarded by
 * availableForWrite(): a line that does not fit is dropped and counted, never
 * waited for, so an unread port can never stall the UI.
 * ========================================================================*/
#pragma once
#include <Arduino.h>

#define LOG_SD_MHZ          16       /* safe on the FPC's long MISO run      */
#define LOG_SYNC_MS       2000
#define LOG_TRIG_TICKS       3       /* ~40 ms above threshold to trigger    */
#define LOG_PRETRIG_TICKS   38       /* ~0.5 s of history before the trigger */
#define LOG_REARM_MS      2000
#define LOG_IDLE_STOP_MS 10000

enum LogState : uint8_t {
  LOGST_NO_CARD,      /* not mounted (or mount failed)                        */
  LOGST_IDLE,         /* mounted, not recording, trigger not waiting          */
  LOGST_WAITING,      /* CURRENT mode, armed for the threshold                */
  LOGST_REARM,        /* CURRENT mode, waiting for current to drop            */
  LOGST_RECORDING,
  LOGST_ERROR         /* a write failed; card unmounted                       */
};

void logBegin();             /* after the display is up — mounts the card     */
void logTick();              /* every loop(): drain ring, trigger, write      */

bool logMount();             /* (re)mount + read-back test. Blocks up to ~2 s */
bool logStart();             /* manual start (mounts if needed)               */
void logStop(const char *reason = "manual");
bool logRecording();
LogState logState();
const char *logStateText();  /* short, upper-case, for the LOG screen          */
const char *logFileName();   /* current or last file, "" if none              */
const char *logCardText();   /* "SD 7.4G FAT32" / "NO CARD" / error text      */
uint32_t logElapsedMs();     /* data time in the current/last log             */
uint32_t logRows();
uint32_t logDrops();         /* ring drops + SD buffer overflows, this log    */
void     logModeChanged();   /* call after gSet.logMode changes               */

void streamSet(bool on);
bool streamOn();
uint32_t streamDrops();
void streamPrintHeader();
