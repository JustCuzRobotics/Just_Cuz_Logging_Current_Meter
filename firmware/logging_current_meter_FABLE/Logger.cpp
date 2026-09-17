/* ==========================================================================
 * Logger.cpp — see Logger.h for the design.
 * ========================================================================*/
#include "Logger.h"
#include "Config.h"
#include "Settings.h"
#include "Sampler.h"
#include "EscOut.h"
#include "Format.h"
#include "Version.h"
#include <SPI.h>
#include <SdFat.h>
#include <string.h>
#include <ctype.h>

static SdFs   sSd;
static FsFile sFile;
static bool   sMounted = false;
static char   sCardText[32] = "NO CARD";
static char   sFileName[40] = "";   /* LOG_123_CURRENT_22.4V.CSV = 25 */
static uint16_t sNextIndex = 1;
static int16_t  sLastVRaw  = 0;       /* newest drained pack voltage, centivolts */
static bool     sHaveVRaw  = false;

enum StartCause : uint8_t { CAUSE_MANUAL, CAUSE_CYCLE, CAUSE_TRIGGER };

static LogState sState = LOGST_NO_CARD;
static bool     sRecording = false;
static bool     sStartReq  = false;         /* next drained record is t = 0  */
static StartCause sCause   = CAUSE_MANUAL;
static bool     sWaitingTrig = false;       /* CURRENT: armed                 */
static bool     sRearm       = false;       /* CURRENT: waiting for low       */
static uint32_t sBelowSinceMs = 0;
static uint8_t  sAboveCount   = 0;

/* per-log */
static uint32_t sT0Ms = 0, sSeq0 = 0;
static uint16_t sStartEpoch = 0;            /* manual/cycle: skip until reset lands */
static uint32_t sFinalDrops = 0;            /* frozen drop count of the last log    */
static int32_t  sLastRelMs = 0;
static uint32_t sRows = 0, sRingDrops0 = 0, sBufDrops = 0;
static float    sMahAcc = 0, sWhAcc = 0, sPrevMah = 0, sPrevWh = 0;
static uint16_t sPrevEpoch = 0;
static uint32_t sLastAboveTMs = 0;
static int16_t  sPeakI = 0;
static int32_t  sPeakMw = 0;
static uint32_t sLastSyncMs = 0;
static uint8_t  sDecim = 1;
static uint32_t sDurMs = 0;

/* SD line buffer, written out in 512 B sector-sized blocks. */
static char   sBuf[8192];
static size_t sBufLen = 0;

/* pre-trigger history (core-0 copy of the most recent records) */
#define HIST_N (LOG_PRETRIG_TICKS + LOG_TRIG_TICKS)
static LogRec   sHist[HIST_N];
static uint16_t sHistHead = 0, sHistCount = 0;

/* stream */
static bool     sStream = false;
static uint32_t sStreamDrops = 0;

/* ---- helpers ---------------------------------------------------------- */

/* Status lines to serial, guarded like telemetry: a monitor that is attached
 * but not being read must never block core 0. */
static void say(const char *fmt, ...) {
  if (!Serial) return;
  char line[160];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof line) n = sizeof line - 1;
  if (Serial.availableForWrite() >= n) Serial.write((const uint8_t *)line, (size_t)n);
}

/* "-12.34" from centi-units, without float. */
static int putCenti(char *p, size_t n, int32_t v) {
  const char *sign = v < 0 ? "-" : "";
  uint32_t a = (uint32_t)(v < 0 ? -v : v);
  return snprintf(p, n, "%s%lu.%02lu", sign, (unsigned long)(a / 100), (unsigned long)(a % 100));
}
/* "123.4" watts from milliwatts. */
static int putWatts(char *p, size_t n, int32_t mw) {
  const char *sign = mw < 0 ? "-" : "";
  uint32_t a = (uint32_t)(mw < 0 ? -mw : mw);
  return snprintf(p, n, "%s%lu.%lu", sign, (unsigned long)(a / 1000), (unsigned long)((a % 1000) / 100));
}

/* The shared row prefix: t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c */
static int fmtCommon(char *p, size_t n, int32_t tMs, const LogRec &r) {
  int k = snprintf(p, n, "%ld,", (long)tMs);
  k += putCenti(p + k, n - k, r.iRaw);  p[k++] = ',';
  k += putCenti(p + k, n - k, r.iFilt); p[k++] = ',';
  k += putCenti(p + k, n - k, r.vRaw);  p[k++] = ',';
  k += putCenti(p + k, n - k, r.vFilt); p[k++] = ',';
  k += putWatts(p + k, n - k, r.mWRaw); p[k++] = ',';
  if (r.tC != FIXED_INVALID) k += putCenti(p + k, n - k, r.tC);
  p[k] = 0;
  return k;
}

static const char *modeName(uint8_t m) {
  switch (m) { case LOGMODE_CYCLE: return "CYCLE"; case LOGMODE_CURRENT: return "CURRENT"; }
  return "MANUAL";
}

static void updateState() {
  if (sRecording || sStartReq)    sState = LOGST_RECORDING;
  else if (sState == LOGST_ERROR) { /* sticky until a mount succeeds */ }
  else if (!sMounted)             sState = LOGST_NO_CARD;
  else if (gSet.logMode == LOGMODE_CURRENT && sRearm)       sState = LOGST_REARM;
  else if (gSet.logMode == LOGMODE_CURRENT && sWaitingTrig) sState = LOGST_WAITING;
  else sState = sMounted ? LOGST_IDLE : LOGST_NO_CARD;
}

/* ---- SD --------------------------------------------------------------- */

/* Next log number = one past the highest already on the card. Recognises the
 * current LOG_<n>_... names and the v3.2-initial LOGnnnn.CSV ones, so the
 * count never restarts on a card that has both. */
static void scanNextIndex() {
  sNextIndex = 1;
  FsFile root, f;
  if (!root.open("/")) return;
  char name[64];
  while (f.openNext(&root, O_RDONLY)) {
    if (!f.isDir()) {
      f.getName(name, sizeof name);
      long idx = -1;
      if (strncasecmp(name, "LOG_", 4) == 0 && isdigit((unsigned char)name[4])) {
        idx = atol(name + 4);
      } else if (strlen(name) == 11 && strncasecmp(name, "LOG", 3) == 0 &&
                 strcasecmp(name + 7, ".CSV") == 0 && isdigit((unsigned char)name[3])) {
        idx = atol(name + 3);
      }
      if (idx >= sNextIndex && idx < 65535) sNextIndex = (uint16_t)(idx + 1);
    }
    f.close();
  }
  root.close();
}

bool logMount() {
  if (sRecording) return true;
  sMounted = false;
  /* SHARED_SPI: SdFat opens and closes a transaction per operation, so the
   * display can run between them. USER_SPI_BEGIN: the display already began
   * SPI0 on our pins — do not re-run SPI.begin() underneath it. */
  if (!sSd.begin(SdSpiConfig(PIN_SD_CS, SHARED_SPI | USER_SPI_BEGIN, SD_SCK_MHZ(LOG_SD_MHZ), &SPI))) {
    snprintf(sCardText, sizeof sCardText, "NO CARD (err 0x%02X)", (unsigned)sSd.sdErrorCode());
    say("# [log] SD mount failed: error 0x%02X data 0x%02X\n",
        (unsigned)sSd.sdErrorCode(), (unsigned)sSd.sdErrorData());
    updateState();
    return false;
  }

  /* Read-back test. The card shares MISO with the LCD on the display FPC; if
   * either part fails to release MISO, writes may still "succeed" while reads
   * come back corrupt. This proves the round trip before a log is trusted. */
  static const char kPattern[] = "JCR read-back 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ \xA5\x5A\xFF\x00!";
  const size_t kLen = sizeof kPattern;
  bool rbOk = false;
  {
    FsFile t;
    if (t.open("JCRTEST.TMP", O_WRONLY | O_CREAT | O_TRUNC)) {
      bool w = t.write((const uint8_t *)kPattern, kLen) == kLen;
      t.close();
      char back[sizeof kPattern];
      if (w && t.open("JCRTEST.TMP", O_RDONLY)) {
        rbOk = (t.read(back, kLen) == (int)kLen) && memcmp(back, kPattern, kLen) == 0;
        t.close();
      }
      sSd.remove("JCRTEST.TMP");
    }
  }
  if (!rbOk) {
    snprintf(sCardText, sizeof sCardText, "SD READ-BACK FAILED");
    say("# [log] SD mounted but read-back test FAILED - check MISO sharing (DESIGN.md s11)\n");
    sState = LOGST_ERROR;
    return false;
  }

  scanNextIndex();
  uint64_t bytes = (uint64_t)sSd.card()->sectorCount() * 512ULL;
  uint8_t ft = sSd.fatType();
  unsigned gb10 = (unsigned)(bytes / 100000000ULL);    /* tenths of a GB */
  snprintf(sCardText, sizeof sCardText, "SD %u.%uG %s", gb10 / 10, gb10 % 10,
           ft == FAT_TYPE_EXFAT ? "EXFAT" : (ft == 32 ? "FAT32" : "FAT16"));
  sMounted = true;
  if (sState == LOGST_ERROR) sState = LOGST_IDLE;
  /* A failure path may have left the trigger disarmed; a good mount always
   * returns CURRENT mode to a clean re-arm. */
  if (gSet.logMode == LOGMODE_CURRENT && !sWaitingTrig) { sRearm = true; sBelowSinceMs = millis(); }
  say("# [log] %s mounted, read-back OK, next log number %02u\n", sCardText, (unsigned)sNextIndex);
  updateState();
  return true;
}

/* snprintf that can never walk k past the buffer, however long a line gets. */
static void hcat(char *h, size_t cap, size_t &k, const char *fmt, ...) {
  if (k >= cap - 1) return;
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(h + k, cap - k, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  k += (size_t)n;
  if (k > cap - 1) k = cap - 1;
}

static void writeHeader() {
  char h[768];
  size_t k = 0;
  hcat(h, sizeof h, k, "# JCR Logging Current Meter %s built %s %s\n",
                FW_VERSION, __DATE__, __TIME__);
  uint8_t durMin = LOG_DUR_MIN[gSet.logDurIdx];
  char dur[16];
  if (durMin) snprintf(dur, sizeof dur, "%u min", (unsigned)durMin); else snprintf(dur, sizeof dur, "until stopped");
  hcat(h, sizeof h, k, "# file %s  start=%s  mode=%s  threshold=%u A  duration=%s  rate=%s (every %u ticks of 13.158 ms)\n",
                sFileName, sCause == CAUSE_TRIGGER ? "current trigger" : (sCause == CAUSE_CYCLE ? "cycle" : "manual"),
                modeName(gSet.logMode), (unsigned)gSet.logThreshA, dur,
                LOG_RATE_LABEL[gSet.logRateIdx], (unsigned)sDecim);
  hcat(h, sizeof h, k, "# filter=%u samples (i_filt,v_filt only)  esc_frame=%u us  cal: V %s, I zero %s, I gain %s\n",
                (unsigned)FILTER_SAMPLES[gSet.filterIndex], (unsigned)ESC_FRAME_US,
                V_CAL_VALID ? "fitted" : "NOMINAL", I_ZERO_VALID ? "measured" : "NOMINAL",
                I_GAIN_VALID ? "fitted" : "NOMINAL - currents not gain-calibrated");
  hcat(h, sizeof h, k, "# t_ms from log start (negative = pre-trigger)  w = v_raw*i_raw  mah/wh since log start  esc_us 0 = output off\n");
  hcat(h, sizeof h, k, "t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,mah,wh,esc_us\n");
  if (sBufLen + k <= sizeof sBuf) { memcpy(sBuf + sBufLen, h, k); sBufLen += k; }
}

static void failWrite(const char *what) {
  say("# [log] SD %s FAILED - log closed, card unmounted\n", what);
  if (sFile.isOpen()) sFile.close();
  sRecording = false; sStartReq = false;
  sMounted = false;
  sBufLen = 0;
  snprintf(sCardText, sizeof sCardText, "SD %s FAILED", what);
  sFinalDrops = (gLogRingDrops - sRingDrops0) + sBufDrops;
  sState = LOGST_ERROR;
  /* Without this, CURRENT mode would sit disarmed forever after a failure. */
  sWaitingTrig = false;
  sRearm = (gSet.logMode == LOGMODE_CURRENT);
  sBelowSinceMs = millis();
}

/* At most `maxBlocks` 512 B writes per call — bounds how long one loop pass
 * can spend on the card. */
static void flushBlocks(uint8_t maxBlocks) {
  while (sBufLen >= 512 && maxBlocks--) {
    if (sFile.write((const uint8_t *)sBuf, 512) != 512) { failWrite("WRITE"); return; }
    sBufLen -= 512;
    memmove(sBuf, sBuf + 512, sBufLen);
  }
}

/* `allowMount`: only a manual start may mount here. An automatic start (the
 * trigger, a cycle) fires as a motor spins up, and a mount with no card
 * blocks core 0 — UI, escTick, the DISARM button — for ~2 s. */
static bool openNewFile(bool allowMount, int16_t centivoltsAtStart) {
  if (!sMounted && !(allowMount && logMount())) return false;
  const char *mode = modeName(gSet.logMode);
  int32_t dv = centivoltsAtStart < 0 ? 0 : (centivoltsAtStart + 5) / 10;   /* 0.1 V */
  for (uint16_t tries = 0; tries < 50; tries++, sNextIndex++) {
    /* LOG_<n>_<MODE>_<V>V.CSV — n is at least two digits and simply grows
     * past 99. Long names need SdFat's LFN support, which SdFs has on. */
    snprintf(sFileName, sizeof sFileName, "LOG_%02u_%s_%ld.%ldV.CSV",
             (unsigned)sNextIndex, mode, (long)(dv / 10), (long)(dv % 10));
    if (sFile.open(sFileName, O_WRONLY | O_CREAT | O_EXCL)) { sNextIndex++; return true; }
  }
  failWrite("CREATE");
  return false;
}

/* Shared by manual, cycle and trigger starts. Opens the file and resets the
 * energy counters; t = 0 is set by the caller (the next record, or the
 * trigger record). */
static bool beginLog(StartCause cause, int16_t centivoltsAtStart) {
  sCause = cause;
  if (!openNewFile(cause == CAUSE_MANUAL, centivoltsAtStart)) {
    if (cause != CAUSE_MANUAL) say("# [log] auto start skipped - no card mounted\n");
    updateState();
    return false;
  }
  sDecim  = LOG_RATE_DECIM[gSet.logRateIdx];
  sDurMs  = (uint32_t)LOG_DUR_MIN[gSet.logDurIdx] * 60000UL;
  sRows = 0; sBufDrops = 0; sLastRelMs = 0;
  sRingDrops0 = gLogRingDrops;
  sMahAcc = sWhAcc = 0; sPeakI = 0; sPeakMw = 0;
  sBufLen = 0;
  sLastSyncMs = millis();
  sStartEpoch = gEnergyEpoch;       /* records still carrying this are pre-reset */
  sFinalDrops = 0;
  gCmdResetEnergyTimer = true;      /* screen and file both start from zero */
  writeHeader();
  sWaitingTrig = false; sRearm = false;
  say("# [log] recording %s (%s)\n", sFileName,
      cause == CAUSE_TRIGGER ? "current trigger" : (cause == CAUSE_CYCLE ? "cycle start" : "manual"));
  return true;
}

static void writeRow(const LogRec &r, int32_t relMs, float mah, float wh) {
  char line[128];
  int k = fmtCommon(line, sizeof line, relMs, r);
  k += snprintf(line + k, sizeof line - k, ",%.2f,%.4f,%u\n", (double)mah, (double)wh, (unsigned)r.escUs);
  if (sBufLen + (size_t)k > sizeof sBuf) { sBufDrops++; return; }
  memcpy(sBuf + sBufLen, line, (size_t)k);
  sBufLen += (size_t)k;
  sRows++;
}

/* Every record while recording goes through here (decimation only decides
 * whether it becomes a row). Energy is accumulated from per-record deltas, so
 * a reset from Live View mid-log cannot make the file's total jump. */
static void recordSample(const LogRec &r) {
  if (r.energyEpoch == sPrevEpoch) {
    sMahAcc += r.mah - sPrevMah;
    sWhAcc  += r.wh  - sPrevWh;
  } else {
    sMahAcc += r.mah;                /* counter restarted from zero        */
    sWhAcc  += r.wh;
  }
  sPrevMah = r.mah; sPrevWh = r.wh; sPrevEpoch = r.energyEpoch;

  int32_t rel = (int32_t)(r.tMs - sT0Ms);
  if (r.iRaw > sPeakI)  sPeakI = r.iRaw;
  if (r.mWRaw > sPeakMw) sPeakMw = r.mWRaw;
  if (r.iRaw >= (int16_t)(gSet.logThreshA * 100)) sLastAboveTMs = r.tMs;

  if (((r.seq - sSeq0) % sDecim) == 0) writeRow(r, rel, sMahAcc, sWhAcc);
  sLastRelMs = rel;
}

void logStop(const char *reason) {
  if (!sRecording && !sStartReq) return;
  bool wasRecording = sRecording;
  sFinalDrops = (gLogRingDrops - sRingDrops0) + sBufDrops;
  sStartReq = false;
  sRecording = false;
  if (wasRecording && sFile.isOpen()) {
    uint32_t ringDrops = gLogRingDrops - sRingDrops0;
    char f[256];
    char pk[16], pw[16];
    putCenti(pk, sizeof pk, sPeakI);
    putWatts(pw, sizeof pw, sPeakMw);
    int k = snprintf(f, sizeof f,
                     "# end reason=%s rows=%lu data_ms=%ld mah=%.2f wh=%.4f peak_i=%s peak_w=%s ring_drops=%lu buffer_drops=%lu\n",
                     reason, (unsigned long)sRows, (long)sLastRelMs, (double)sMahAcc, (double)sWhAcc,
                     pk, pw, (unsigned long)ringDrops, (unsigned long)sBufDrops);
    if (sBufLen + (size_t)k <= sizeof sBuf) { memcpy(sBuf + sBufLen, f, (size_t)k); sBufLen += (size_t)k; }
    bool ok = true;
    if (sBufLen) ok = sFile.write((const uint8_t *)sBuf, sBufLen) == sBufLen;
    sBufLen = 0;
    ok = sFile.close() && ok;
    if (!ok) { failWrite("CLOSE"); return; }
    say("# [log] closed %s: %s, %lu rows, %ld ms, %.1f mAh, %.3f Wh, drops %lu/%lu\n",
        sFileName, reason, (unsigned long)sRows, (long)sLastRelMs, (double)sMahAcc, (double)sWhAcc,
        (unsigned long)ringDrops, (unsigned long)sBufDrops);
  } else if (sFile.isOpen()) {
    sFile.close();          /* a start that never saw its first record */
    sSd.remove(sFileName);
    sFileName[0] = 0;
  }
  if (gSet.logMode == LOGMODE_CURRENT) { sRearm = true; sBelowSinceMs = millis(); sAboveCount = 0; }
  updateState();
}

/* Pack voltage for a manual/cycle file name: the newest drained sample, or
 * the displayed value if nothing has been drained yet (a start in the first
 * pass after boot). */
static int16_t startVolts() { return sHaveVRaw ? sLastVRaw : gState.centivolts; }

bool logStart() {
  if (sRecording || sStartReq) return true;
  if (!beginLog(CAUSE_MANUAL, startVolts())) return false;
  sStartReq = true;
  updateState();
  return true;
}

/* ---- trigger ---------------------------------------------------------- */

static void histPush(const LogRec &r) {
  sHist[sHistHead] = r;
  sHistHead = (uint16_t)((sHistHead + 1) % HIST_N);
  if (sHistCount < HIST_N) sHistCount++;
}

/* Called for each drained record while not recording in CURRENT mode. */
static void triggerSample(const LogRec &r) {
  int16_t th = (int16_t)(gSet.logThreshA * 100);
  bool above = r.iRaw >= th;

  if (sRearm) {
    if (above) sBelowSinceMs = millis();
    else if (millis() - sBelowSinceMs >= LOG_REARM_MS) { sRearm = false; sWaitingTrig = true; }
    sAboveCount = 0;
    return;
  }
  if (!sWaitingTrig) return;

  sAboveCount = above ? (uint8_t)(sAboveCount + 1) : 0;
  if (sAboveCount < LOG_TRIG_TICKS) return;
  sAboveCount = 0;

  /* Triggered. The first above-threshold record is t = 0; everything older in
   * the history is pre-trigger. The file is named with the voltage of the
   * last sample BEFORE the load came on — the resting pack voltage, which
   * says more about the pack than the sag at the trigger instant does. */
  const LogRec &trigFirst = sHist[(sHistHead + HIST_N - LOG_TRIG_TICKS) % HIST_N];
  int16_t restV = sHistCount > LOG_TRIG_TICKS
      ? sHist[(sHistHead + HIST_N - LOG_TRIG_TICKS - 1) % HIST_N].vRaw
      : trigFirst.vRaw;
  if (!beginLog(CAUSE_TRIGGER, restV)) {
    sWaitingTrig = false; sRearm = true; sBelowSinceMs = millis();
    return;
  }
  uint16_t n = sHistCount;
  uint16_t oldest = (uint16_t)((sHistHead + HIST_N - n) % HIST_N);
  const LogRec &first = sHist[(sHistHead + HIST_N - LOG_TRIG_TICKS) % HIST_N];
  sT0Ms = first.tMs; sSeq0 = first.seq;
  sPrevEpoch = first.energyEpoch; sPrevMah = first.mah; sPrevWh = first.wh;
  sLastAboveTMs = r.tMs;
  sRecording = true;
  for (uint16_t i = 0; i < n; i++) {
    const LogRec &h = sHist[(oldest + i) % HIST_N];
    int32_t rel = (int32_t)(h.tMs - sT0Ms);
    if (rel < 0) {
      /* Pre-trigger: energy is zero by definition (t = 0 is the start). */
      if (((sSeq0 - h.seq) % sDecim) == 0) writeRow(h, rel, 0.0f, 0.0f);
      sLastRelMs = rel;
    } else {
      recordSample(h);
    }
  }
  sHistCount = 0;
}

/* ---- per-loop --------------------------------------------------------- */

static void streamRow(const LogRec &r) {
  if (!sStream || !Serial) return;
  if ((r.seq % LOG_RATE_DECIM[gSet.logRateIdx]) != 0) return;
  char line[100];
  int k = fmtCommon(line, sizeof line, (int32_t)r.tMs, r);
  k += snprintf(line + k, sizeof line - k, ",%u\n", (unsigned)r.escUs);
  if (Serial.availableForWrite() >= k) Serial.write((const uint8_t *)line, (size_t)k);
  else sStreamDrops++;
}

void logBegin() {
  sStream = gSet.streamOn != 0;
  logMount();
  logModeChanged();
}

void logModeChanged() {
  sWaitingTrig = false; sAboveCount = 0; sHistCount = 0;
  sRearm = (gSet.logMode == LOGMODE_CURRENT) && !sRecording;
  sBelowSinceMs = millis();
  updateState();
}

void logTick() {
  /* CYCLE mode follows Test Mode's cycle. */
  static bool lastCycling = false;
  bool cyc = escCycling();
  if (cyc != lastCycling) {
    lastCycling = cyc;
    if (gSet.logMode == LOGMODE_CYCLE) {
      if (cyc && !sRecording && !sStartReq) {
        if (beginLog(CAUSE_CYCLE, startVolts())) sStartReq = true;
      } else if (!cyc && (sRecording || sStartReq) && sCause == CAUSE_CYCLE) {
        logStop("cycle stopped");
      }
    }
  }

  /* Drain, bounded per pass. */
  for (uint16_t budget = 64; budget && gLogTail != gLogHead; budget--) {
    /* Back-pressure: if the SD buffer is nearly full, write blocks now; if the
     * card still cannot keep up, stop draining and leave the records in the
     * 6.7 s ring rather than drop rows from a full buffer. */
    if (sRecording && sBufLen > sizeof sBuf - 256) {
      flushBlocks(4);
      if (sRecording && sBufLen > sizeof sBuf - 256) break;
    }
    const LogRec &r = gLogRing[gLogTail];
    sLastVRaw = r.vRaw; sHaveVRaw = true;

    streamRow(r);

    if (sStartReq) {
      /* Skip records pushed before core 1 applied the energy reset (they are
       * still in the ring after the flag clears); the first record of the new
       * epoch is t = 0. */
      if (r.energyEpoch != sStartEpoch) {
        sStartReq = false; sRecording = true;
        sT0Ms = r.tMs; sSeq0 = r.seq;
        sPrevEpoch = r.energyEpoch; sPrevMah = r.mah; sPrevWh = r.wh;
        sLastAboveTMs = r.tMs;
        recordSample(r);
      }
    } else if (sRecording) {
      int32_t rel = (int32_t)(r.tMs - sT0Ms);
      if (sDurMs && rel > (int32_t)sDurMs) {
        logStop("duration");
      } else if (!sDurMs && sCause == CAUSE_TRIGGER &&
                 (r.tMs - sLastAboveTMs) >= LOG_IDLE_STOP_MS) {
        logStop("below threshold");
      } else {
        recordSample(r);
      }
    } else if (gSet.logMode == LOGMODE_CURRENT) {
      histPush(r);
      triggerSample(r);
    }

    gLogTail = (uint16_t)((gLogTail + 1) % LOG_RING_N);
  }

  if (sRecording) {
    flushBlocks(4);
    if (sRecording && millis() - sLastSyncMs >= LOG_SYNC_MS) {
      sLastSyncMs = millis();
      if (!sFile.sync()) failWrite("SYNC");
    }
  }
  updateState();
}

/* ---- accessors -------------------------------------------------------- */

bool        logRecording() { return sRecording || sStartReq; }
LogState    logState()     { return sState; }
const char *logFileName()  { return sFileName; }
const char *logCardText()  { return sCardText; }
uint32_t    logElapsedMs() { return sLastRelMs > 0 ? (uint32_t)sLastRelMs : 0; }
uint32_t    logRows()      { return sRows; }
uint32_t    logDrops()     { return sRecording ? (gLogRingDrops - sRingDrops0) + sBufDrops : sFinalDrops; }

const char *logStateText() {
  switch (sState) {
    case LOGST_NO_CARD:   return "NO CARD";
    case LOGST_IDLE:      return "READY";
    case LOGST_WAITING:   return "WAITING";
    case LOGST_REARM:     return "RE-ARMING";
    case LOGST_RECORDING: return "RECORDING";
    case LOGST_ERROR:     return "SD ERROR";
  }
  return "";
}

void streamPrintHeader() {
  say("# stream: t_ms since boot; w = v_raw*i_raw; energy is logged to SD only\n");
  say("t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,esc_us\n");
}
void streamSet(bool on) {
  sStream = on;
  if (on) streamPrintHeader();
}
bool     streamOn()    { return sStream; }
uint32_t streamDrops() { return sStreamDrops; }
