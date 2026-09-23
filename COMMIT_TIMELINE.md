# Detailed Commit Timeline

The complete account of every commit, newest first. Commit messages stay short
and scannable; this file holds the full detail — what changed in each file, why,
how it was verified, and what remains open. Entries are grouped by version where
the project has one, otherwise by date, so a section can be lifted into release
notes with light editing.

<!-- Newest entries go directly below this line. -->

## v3.5 — 2026-09-23

### firmware: v3.5 — ESC pre-roll, and stopping without disarming

**Why:** the first bench run of v3.3 found three things. An ESC beeps for about four seconds
after the signal comes up before it will spin, so a cycle that began at once ate the start of
its own first ramp and the motor jerked. Pressing START with the output already live did
nothing on the LOG TEST tab (it refused, asking to be disarmed first). And stopping meant
cutting the signal, so every restart paid the ESC's start-up again — worse on ESCs with
start-up music. Decisions taken with Seth: the pre-roll is a Settings value, not a constant,
because start-up time is a property of the ESC; a tap stops the motor but keeps the signal,
with a hold to cut; a finished Log Test stays live at idle with a fuse behind it.

**Changes**

- `EscProfile.h` — `PH_ARMING` is gone: `PH_PRE` is now the pre-roll before **every** run,
  cycle or Log Test, and its length is `EscProfile.preMs`, snapshotted at start like the rest
  of the profile.
- `Settings.h/.cpp` — blob v5 (36 B, `static_assert`ed) adds `preRollMs` (0–15000 ms, default
  5000, 500 ms steps) and an explicit `reserved1` so the size is stated rather than implied by
  the uint32's alignment. v4, v3, v2 and v1 blobs migrate.
- `EscOut.h/.cpp` — stopping is now two levels. `escNeutral()` stops a run or a throttle and
  leaves the signal up at idle (synchronously: the idle pulse is written there and then, since
  a caller may block for seconds opening a log). `escCut()` is the old hard stop. A run that
  ends on its own leaves the output live at idle with a 30 s fuse; a stop asked for by hand
  gets a 5 minute one; touching the throttle or starting anything cancels it. `escCycleStart()`
  works with the output already live — it commands idle and pre-rolls instead of refusing —
  and the Log Test end holds neutral instead of disarming.
- `ScreenTestCommon.cpp` — the header button reads ARM / STOP / CUT and escalates. Hold-to-cut
  reuses the press-and-hold repeat, but only for a press that started on STOP, so holding ARM
  can no longer arm the ESC and cut it a second later.
- `logging_current_meter_FABLE.ino` — a held button that the finger has slid off no longer
  repeats (`serviceHeldTarget()` re-runs the hit test), which matters now that a repeat can
  cut the output.
- `ScreenTestCycle.cpp` — START LOG TEST commands neutral instead of refusing when armed, so
  the card mount can never block with a motor under throttle; the tab's idle line states the
  real pre-roll and that the output ends live at idle.
- `Layout.h` / `ScreenSettings.cpp` — a sixth Settings row, ESC PRE-ROLL, with ± steppers and
  hold-to-repeat; DUMP and DEV MODE share the last row to make space.
- **Dwells now run to 3 minutes** (`PROF_DWELL_MAX_MS` 180000, up from 15000), for holding a
  motor at load long enough to get thermal data. That needs 32 bits, so `profDwellHiMs` /
  `profDwellLoMs` moved to `uint32_t` in both `Settings` (blob **v6**, 40 B, migrating v5) and
  `EscProfile` — whose field ORDER is now documented as part of its interface, because the host
  tests build profiles with aggregate initialisers. The cycle editor reads and writes tiles
  through `tileGet()` / `tilePut()` on a common `uint32_t` rather than a `uint16_t*`, and
  millisecond tiles change gear with their value: 50/500 ms below 5 s, 1 s/10 s above it, with
  the buttons relabelling themselves (`+1 S`, `+10 S`) as the threshold is crossed.

**Verification:** compile clean, zero warnings (154.0 KB). Engine host tests extended: the
pre-roll length comes from the profile, a cycle started while armed drops to idle immediately
and does not re-arm, neutral is synchronous, neutral declines during a post-roll (so the
button falls through to a cut), a hand stop leaves the long fuse, throttle cancels it, and the
30 s fuse cuts a finished test. The clock harness covers the v5→v6 migration, a 3-minute dwell
through a save/load round trip, and a dwell past the cap rejecting the blob. Independent review
found eight issues, all fixed — the worst were neutral not reaching the pin before a blocking
card mount, and a dead header button during a powered post-roll.

**Open:** not bench-tested. Seth to check the 5 s pre-roll against his ESC's beeps, START from
armed, hold-to-cut, and that a finished test ends quiet.

## v3.4 — 2026-09-23

### firmware: v3.4 — a settable wall clock, stamped into every log

**Why:** logs carried `t_ms` from log start and a file counter, and nothing said when a run
happened; comparing a week of pulls meant remembering which file was which day. The board has
no RTC and no backup cell, so this is a software clock: an epoch base plus `millis()`, set
from the screen or from the laptop, and copied into flash often enough that a power cycle
comes back knowing the date. Decisions taken with Seth: save every 5 minutes while idle; date
and time in the CSV metadata only, not in file names; a serial command accepting both epoch
seconds and a typed date; and its own SET CLOCK screen rather than a cramped Settings row.

**Changes**

- `Clock.h/.cpp` — new. Hinnant civil-from-days date maths (no tables, no loops, exact),
  `clockNow()` = base + elapsed with the base carried forward before the 49.7-day `millis()`
  wrap (otherwise the clock would jump backwards seven weeks and then be saved), state
  UNSET / RESTORED / SET, and a plausibility window of 2020–2099 applied on load, on the
  screen and over serial.
- `Settings.h/.cpp` — blob v4 (32 bytes, `static_assert`ed): `clockEpoch` and `clockEverSet`
  ahead of the existing fields so the 4-byte value cannot introduce padding. v3, v2 and v1
  blobs migrate. `settingsSaveClock()` writes the **last saved** blob with only the clock
  refreshed, from a `s_flashCopy` kept at load and save time, so a periodic clock save can
  never commit screen edits nobody pressed SAVE for.
- `ScreenClock.cpp`, `Layout.h`, `Screens.*` — new SET CLOCK screen: YEAR / MONTH / DAY /
  HOUR / MIN tiles, the Test Mode editor row (±1 / ±10, big steps in lime, hold to repeat),
  APPLY. Fields wrap; day is clamped to the month, leap years included. The working copy is
  seeded on entry only, because a toast expiring repaints the screen and would otherwise wipe
  an edit in progress. Settings goes to six rows at a 44 px pitch for the CLOCK row, which
  shows the time and how far to trust it.
- `logging_current_meter_FABLE.ino` — serial `c`: the one bench key that takes an argument,
  so it collects a line (newline, or a 2 s gap between characters — typing a date by hand
  takes longer than 2 s from the first keystroke). `clockSaveBlocked()` / `clockSaveSoon()` /
  `clockSaveTick()` own the save policy: an EEPROM commit stalls both cores, so nothing
  writes flash while a log records or the ESC is live, and a save asked for then is written
  at the next idle pass.
- `Logger.cpp` — `# clock <time> local epoch=… state=…` header line and `stopped=<ISO>` on
  the `# end` line (a `T`, not a space, so the key=value parse survives). Log start and stop
  request a clock save rather than committing inside the capture path.
- `tools/log_analyzer.py` — parses the clock line; the stats box shows `Started:` with a
  "clock restored - may be behind" note when the time is not trustworthy, and the summary CSV
  carries started/stopped and the clock state. Older logs are unaffected.
- `tools/capture_stream.py` — sends `c <epoch>` on connect (`--no-set-clock` to skip), so a
  session with the laptop attached is stamped to the second.

**Verification:** compile clean for `waveshare_rp2040_zero`, zero warnings (152.6 KB).
New host test (`hosttest/clock`): epoch↔civil against `gmtime_r` every 6 h from 2020 to 2100,
leap years and month lengths, formatting, the `millis()` wrap over 60 simulated days,
sub-second truncation, the 2020–2099 window, blob v4 round-trip, v3→v4 migration, a rotted
stored epoch rejected, and the clock-only save leaving unsaved screen edits out of flash.
Logger harness confirms the header line and `stopped=`; engine tests still pass. Layout
previews rendered for SETTINGS and SET CLOCK with the hit-rect overlap check (which also
found a pre-existing filter-minus hit rect that did not cover its button, now fixed).
Reviewed by an independent pass; seven findings fixed — EEPROM commits inside the capture
path, the toast-repaint wiping an edit, the missing ESC guard, the 49.7-day wrap, the missing
upper bound on the epoch, the line-collector timeout, and a truncated toast.

**Open:** not bench-tested. Seth to check a power cycle restores the date, that a serial sync
lands to the second, and that no save ever lands mid-capture.

## v3.3 — 2026-09-22

### JCR_TouchScreen 1.2.0: lastGoodSampleMicros() for fail-safe held controls

**Why:** v3.3's dead-man slider must drop the throttle if the finger stops being reported.
Review of `JCR_FT6336::serviceNow()` showed that an I2C error, an impossible contact count or
an out-of-range point all return before the release logic and before the live state is
republished: `isDown()` then stays true with a frozen point, and no UP event is ever queued.

**Changes:** `JCR_FT6336.h/.cpp` — `volatile uint32_t _pubAtMicros`, stamped after each
successful publish (a single aligned store, atomic on the M0+), exposed as
`lastGoodSampleMicros()`. README Diagnostics section documents it. `library.properties`
1.1.0 → 1.2.0. No behaviour change for existing callers.

**Verified:** compiles into firmware v3.3 with zero warnings. Not yet exercised on hardware.

### firmware: v3.3 — Test Mode rework: manual / ramped cycle / logged cycle test

**Why:** after bench use of v3.2a, Test Mode was clunky: arming only worked at 1000 µs (a
bidirectional ESC needs 1500 µs neutral), manual control was repeated tapping, and the cycle
stepped instantly to full throttle, which threw motors off the bench. Seth asked for separate
Manual / Cycle / Logging Cycle Test screens, a slider option, linear ramps, a counted test that
logs itself with a 3 s pre-roll and 5 s post-roll, press-and-hold on value buttons, and a
reset-to-defaults. Decisions taken with him: a UNI/BIDI ESC type setting; BIDI direction
FWD / REV / FWD+REV per test; HOLD / DEAD-MAN slider release; auto-disarm after a Log Test;
three tabs sharing one saved profile; tile + shared editor row for editing.

**Changes**

- `EscProfile.h` — new. Pure profile maths with no Arduino dependency: phases, phase lengths,
  `profilePulse()` (linear ramps with rounding, clamped at their ends, 0 ms = step), BIDI
  mirroring about 1500 µs, per-cycle direction.
- `EscOut.h/.cpp` — rewritten as a motion engine over the same Servo/PIO output. Phases OFF /
  MANUAL / ARMING / PRE / RAMP_UP / DWELL_HI / RAMP_DN / DWELL_LO / POST. Idle follows the
  ESC type. Profile snapshotted at run start. `escArm(false)` (header STOP) cuts the output in
  every state; a Log Test then finishes an unpowered post-roll. The detach path first queues
  idle, so the one pulse `Servo::detach()` can still let out is harmless. Phase starts advance
  by nominal length through jitter but resync after a stall over 50 ms (catch-up would skip
  a short dwell). `escTick()` returns `ESC_EV_TEST_DONE`.
- `ScreenTestCommon.cpp`, `ScreenTestManual.cpp`, `ScreenTestCycle.cpp` — new; `ScreenTest.cpp`
  removed. Header: Back, three tabs, ARM/STOP; status line. MANUAL: ESC type (locked while
  armed; arrow icon + ONE DIRECTION / BIDIRECTIONAL), BUTTONS/SLIDER, HOLD/DEAD-MAN, big readout with % and FWD/REV, IDLE. CYCLE and LOG
  TEST: 8 tiles, editor row with per-tile steps (µs 10/50, ms 50/500, cycles 1/10, direction
  ◀ ▶), two-tap RESET DEFAULTS, START/STOP. LOG TEST shows the file and rows; CYCLE says
  whether LOG MODE will log it.
- `Layout.h` — TEST MODE section replaced (header, manual and cycle targets; `TH_N` is a
  `constexpr int` so the tab enums avoid C++20 enum-enum arithmetic warnings).
- `Widgets.*` — `drawLabelBtn` (5x7 x2 label, scale 1 fallback), `drawTile`, `drawDeltaBtn`
  (the coarse button of a small/big pair gets lime border, text and a second ring),
  `drawEscTypeBtn` (single- or double-headed arrow plus the words).
- `Theme.*` — `bigStep` palette entry (`COL_BIG_STEP`, lime 0x87F0 in both themes).
- `Screens.*` — three Test screen ids, dispatch/hit/tick routing, `screenRepeatable` /
  `screenIsDrag` / `screenDrag` / `screenRelease`, and an overlap check covering the header
  plus each manual mode's target set.
- `logging_current_meter_FABLE.ino` — hold-to-repeat (1 s, then every 200 ms, no bursting after
  a slow frame) for manual steppers, the cycle editor, and the LOG and Settings steppers.
  Repeats only while the press is effective, so a refused press doesn't re-toast. Slider drag
  from `getTouch()` every pass. A held drag is released on UP, on `!isDown()`, on a new DOWN
  (lost UP), on a screen change, or after 60 ms without a good touch sample. The Log Test end
  closes the log only if the test still owns it, after `logTick()` so the post-roll's last
  samples are written. Serial `g` is refused during a test.
- `Settings.h/.cpp` — blob v3 (26 bytes, `static_assert`ed): ESC type, control style, release
  mode, direction, last tab, and the profile. v2 and v1 blobs migrate: old dwells are floored
  at 500 ms and ramps default to 1000 ms. No live throttle is stored at all.
  `settingsProfileDefaults`, `settingsFixProfileForType`.
- `Logger.*` — `CAUSE_TEST`, `logStartTest()`, `logTestActive()`; files `LOG_<n>_TEST_<V>V.CSV`;
  header `mode=TEST`, `duration=test profile` and a `# profile …` line; buffer 1 KB; DURATION
  limit exempt. `ScreenLog.cpp` refuses START/STOP while a test runs.
- `tools/log_analyzer.py` — parses `# profile`, adds it to the stats box, and draws an ESC-pulse
  strip under the main chart whenever `esc_us` is non-zero.
- `Version.h` → `v3.3 (FABLE)`.

**Verified:**
- Compiles clean on arduino-pico 6.1.0, zero warnings in project files: 143.6 KB flash,
  48.8 KB RAM.
- Host unit test of `EscOut` + `Settings` under ASan/UBSan:
  - ramp values, rounding and clamping; BIDI mirroring
  - arm hold at 1000 and 1500 µs
  - continuous cycle with no step over 5 µs between 5 ms loop ticks
  - a 3-cycle test with 3 s pre / 5 s post, done event and disarm
  - abort into an idle post-roll; header STOP into an unpowered post-roll with re-arm refused
  - FWD+REV alternation; 0 ms ramps
  - a 700 ms stall not skipping a 500 ms dwell
  - v2 → v3 migration and a v3 round-trip
- Logger harness: a TEST file with its profile line, a second start refused, a test running
  past a 1-minute DURATION setting, `reason=complete`.
- The three layouts were rendered to PNG from `Layout.h` and checked for hit-rect overlaps
  and coverage.
- An independent review found 11 issues; all were fixed before this commit. They were:
  - dead-man stuck on a hung touch controller or a lost UP
  - the DEAD-MAN toggle leaving a held throttle
  - the test end closing an unrelated log
  - a finger held through the arming hold jumping the throttle
  - a card mount blocking while armed
  - lost quick taps on the slider
  - stall catch-up skipping a phase
  - the extra pulse on detach
  - re-toasting on hold
  - the wrong duration text in the test header
  - overlap-check coverage

**Not yet bench-verified.**

**Open:**
- Bench-check UNI and BIDI arming, slider drag and dead-man release, hold-repeat, and a
  3-cycle LOG TEST (on a scope or with the motor clamped).
- Whether the 50 Hz frame gives smooth enough ramps (about 50 steps per second).
- A manual slew limit, if the slider proves too sharp.

## v3.2a — 2026-09-16

### tools: log_analyzer.py — graph SD logs, summarise and compare runs

**Why:** logs are only useful once they're read. Seth wanted the equivalent of his EM clutch
dyno analyzer (`Analyzer-V1.5.py`) for meter logs: pick a file or a folder in a GUI, and get
graphs and stats saved automatically.

**Changes**

- `tools/log_analyzer.py` — new. tkinter GUI (Open Log File(s) / Open Folder analyze
  immediately; tick 2–3 rows and Compare Selected; "Open graphs after saving" toggle) plus a
  no-GUI mode (`python log_analyzer.py <file-or-folder>`). Saves to `Log Graphs\` beside the logs:
  - `<log>-Current_Voltage_Temp.png` — raw current and voltage thin/faint with filtered bold on
    top, temperature dashed on a third axis, pre-trigger shaded; stats box top right: log mode
    (+ start cause), runtime, mAh/Wh, peak current and its time, peak power, voltage sag
    (resting → minimum; resting = pre-trigger median, else the first 0.25 s), peak temp, and
    notes for uncalibrated current gain or a log with no `# end` line.
  - `<log>-Energy.png` — mAh left axis, Wh right (dashed), both from zero with equal headroom so
    the lines coincide when voltage is steady; totals, average current and power in a box.
  - Two or more logs: `Log Summary.csv` (stats as rows, one column per log — the dyno analyzer's
    layout) and `Log Summary-Comparison.png` (mAh, Wh, peak current, sag bars).
  - Compare: `Compare-LOG_a_vs_LOG_b[_vs_LOG_c]-Overlay.png` — filtered current, voltage and
    mAh on shared time axes with a stats table.
  Styling follows the dyno analyzer (`seaborn-v0_8`, white, black border, 300 DPI); series
  colours are a CVD-validated blue/orange/aqua. Also reads `capture_stream.py` CSVs, integrating
  energy from V × I since the stream carries none. When opening graphs for more than 3 logs it
  opens the comparison chart and the folder rather than every image.

**Verified:** logs generated by the firmware's own `Logger.cpp` in the host harness (current
trigger ×2, cycle, manual, and a manual log with its end line removed) plus a synthetic USB
capture; all charts rendered and inspected; runtime rounding fixed (59.96 s showed as
"0:60.0"); GUI driven under Xvfb with Python 3.12 — open folder → 5 logs listed → compare 2.
First real bench log (LOG_02_MANUAL_22.8V, not stopped before power-off) showed "nan mAh / nan
Wh": its last row was half-written, leaving blank energy columns that the stats read. Rows missing
t_ms/i_raw/v_raw/mah/wh are now dropped (counted in the summary as `dropped_partial_rows` and noted
in the stats box), and totals use the last valid value.

### firmware: v3.2a — descriptive log file names, clearer LOG steppers

**Why:** v3.2 flashed and ran (after turning off Windows Smart App Control, which had started
blocking the unsigned pico toolchain). First bench requests: log files should say what they
are without opening them, and the LOG MODE stepper's +/- buttons read as a quantity when
the control is a three-way choice that stuck at each end. RATE ran backwards (+ lowered the
rate), and DURATION's NO LIMIT at the bottom end gave no hint that + was the way out.

**Changes**

- `Logger.h/.cpp` — files are `LOG_<n>_<MODE>_<V>V.CSV` (`%02u`, grows past 99), e.g.
  `LOG_07_CURRENT_22.4V.CSV`. `MODE` is the log-mode setting (the start cause stays in the
  header). `V` is the pack voltage at start to 0.1 V: for manual and cycle starts the newest
  drained raw sample (falling back to the displayed value before the first record); for a
  current trigger the raw sample just before the first above-threshold one, i.e. the resting
  voltage rather than the sag. The index scan recognises both `LOG_<n>_…` and v3.2's
  `LOGnnnn.CSV`, so numbering never restarts on a card holding both. File name buffer 40 B;
  the longest realistic name is 25 characters and still fits the LOG screen's status line.
  Relies on SdFat long file names (on by default for `SdFs`).
- `Widgets.h/.cpp` — `drawArrowBtn()`: stepper chrome with a filled left/right triangle drawn
  from vertical runs (the TFT library has no triangle primitive). `drawStepBtn()` gains an
  `enabled` flag that draws it in the disabled palette.
- `ScreenLog.cpp` — LOG MODE uses the arrows and wraps MANUAL ⇄ CYCLE ⇄ CURRENT in both
  directions. Rate, threshold and duration keep +/- and still stop at their ends, and the
  button for a direction that can go no further is **greyed out** (so NO LIMIT shows a grey −).
  RATE's buttons are inverted relative to its index — `LOG_RATE_DECIM` runs fast→slow and the
  index is what Settings persists, so the array order was left alone and + now means faster.
  Greying is re-evaluated each tick and repainted only on change.
- `Version.h` → `v3.2a (FABLE)`; `.ino` history entry.

**Verified:** compiles clean on arduino-pico 6.1.0 (134.5 KB flash). Host simulation (ASan +
UBSan): names `LOG_01_MANUAL_24.4V.CSV`, `LOG_02_CURRENT_20.0V.CSV`, … `LOG_04_CYCLE_20.0V.CSV`,
numbering continues across a remount, all earlier logger cases unchanged. Arrows and greyed
buttons not yet seen on the panel.

**Open:** user-defined log names via serial commands (typing on the touchscreen isn't
worth building).

## v3.2 — 2026-09-16

### firmware: v3.2 — fix ESC pulse timing, add SD logging and USB stream

**Why:** Test Mode was bench-tried with two ESCs: one armed, the other never responded,
and after a power cycle neither would arm — while both worked from a WM150 servo tester.
Reading arduino-pico's `wiring_analog.cpp` (checked in 6.1.0 and current master) found the
cause: `analogWriteFreq()` clamps anything under 100 Hz up to 100 Hz with only a
`DEBUGCORE` message. v3.1 asked for 50 Hz with `analogWriteRange(20000)`, so the slice ran
a 10 ms frame across 20000 counts and **every pulse was half width** — 1000 µs idle came out
as 500 µs, 2000 µs as 1000 µs. The same mismatch also re-initialised the slice on every
`applyPwm()`. There was no reason to be on `analogWrite()` at all: the core ships a PIO
Servo library with `writeMicroseconds()`. Separately, the serial `e` key armed at 1500 µs, and the ARM button armed at
whatever the set point was, and ESCs refuse to arm when their first frames carry throttle.
Logging to SD and a live USB stream were the other two items for the day; the meter will
often run in a test box driven by a normal receiver with nobody at the screen, which set
the trigger design.

**Changes**

- `EscOut.h/.cpp` — output via arduino-pico's bundled `Servo` library (PIO,
  `writeMicroseconds()`), fixed 20 ms / 50 Hz frame — Seth's call over keeping a settable
  period on hand-driven PWM. Attached as `attach(pin, 1000, 2000, 1000)` because the
  library's default initial value is 1500 µs. Disarm detaches (the library lets the frame
  in progress finish) then drives the pin low as SIO.
  **Arming always emits 1000 µs for `ESC_ARM_HOLD_MS` (2 s)** and resets the set point to
  idle; stepper presses during the hold are remembered and applied when it ends; a cycle
  started while disarmed arms, holds, then begins at its low phase. `gEscOutUs` publishes
  the pulse on the pin for the log. `escPrint()` dumps the output state.
- `Sampler.h/.cpp` — one `LogRec` per tick (seq, time, raw + filtered I and V, temperature,
  ESC pulse, raw power, cumulative mAh/Wh, energy epoch) pushed into a 512-record SPSC ring
  at the end of `samplerTick()`; on overflow the new record is dropped and counted. An
  energy reset now bumps `gEnergyEpoch` and restarts from the reset tick's own increment
  instead of zero, so that tick is not lost.
- `Logger.h/.cpp` — new, core 0. SdFat (`SdFs`) on SPI0 with `SHARED_SPI | USER_SPI_BEGIN`
  at 16 MHz; mount runs a write/read-back test (MISO sharing, `DESIGN.md` §11) and scans for
  the next `LOGnnnn.CSV`. No pre-allocation — SdFat's `preAllocate()` sets the full file size
  immediately, which would leave junk after the data on a power pull. 8 KB line buffer
  written in 512 B blocks (drain stops and leaves records in the ring if the card falls
  behind), sync every 2 s. Triggers: MANUAL, CYCLE (edge of `escCycling()`), CURRENT (raw
  current ≥ threshold for 3 ticks, 0.5 s pre-trigger history, re-arm after 2 s below).
  Duration 0–15 min; a trigger-started log with no limit ends 10 s below threshold. Log
  start requests the energy/timer reset and skips ring records from the previous epoch;
  file energy accumulates per-record deltas across epochs, so a Live View reset mid-log
  cannot make the total jump. Automatic starts never mount (a no-card mount blocks core 0
  ~2 s). Any SD failure closes the log, unmounts, shows `SD ERROR` and re-arms CURRENT
  mode. USB stream: same rows without energy, `availableForWrite()`-guarded, drops counted.
- `ScreenLog.cpp` — new. Mode / rate / threshold / duration steppers (locked while
  recording), status (state, file, time, rows + drops, card + USB drops), USB toggle, SAVE
  (refused while recording), REMOUNT SD, START/STOP LOG. Threshold box dims outside
  CURRENT mode.
- `Layout.h`, `Screens.*`, `ScreenHome.cpp`, `Widgets.*` — LOG tile enabled and routed;
  `LOG_T` targets (boot overlap check included); `logBar` red strip on the bottom 4 px of
  every screen (solid recording, dashed armed).
- `Settings.h/.cpp` — blob v2 drops `escPeriodUs` and adds log mode, rate, duration,
  threshold (1–50 A, default 5) and stream-at-boot. A v1 (v3.1c) blob is migrated rather
  than discarded (its period is ignored).
- `ScreenTest.cpp` — frame-period stepper replaced by a fixed `20 MS  50 HZ` readout
  (`Layout.h` loses `TEST_PER_M/P` and `TEST_PERIOD_BOX`); status shows `ARMING - IDLE Ns`; the pulse box turns amber only once
  the set point is actually on the pin. `ScreenSettings.cpp` — SAVE refused while recording;
  calibration dump lines `#`-prefixed.
- `logging_current_meter_FABLE.ino` — v3.2 header; `logBegin()` after core 1 is released so
  a no-card mount does not freeze boot; `logTick()`/`logBarTick()` in `loop()`; keys `e`
  (arm at idle), `p`, `s`, `g`, `m`; every non-data serial line prefixed `#`.
  `Version.h` — new, shared version string. `Theme.cpp` — `#` prefix.
- `tools/capture_stream.py` — new. pyserial capture: auto-detects VID 0x2E8A, turns the
  stream on if no data arrives, adds `pc_time`, echoes `#` lines.

**Verified:** compiles clean against arduino-pico 6.1.0 (`waveshare_rp2040_zero`, warnings
on): 134 KB flash, 48.6 KB RAM. The logger was compiled on the host against stub SdFat /
Serial under AddressSanitizer + UBSan and driven with synthetic ticks: manual log (760
rows, 27.7 mAh for 10 A × 10 s), CURRENT trigger at 30 A with pre-trigger rows from
−455 ms, 60 s duration cut-off (506 mAh), no retrigger while still above threshold, re-arm
after 2 s low, write failure → `SD ERROR` → remount → armed again, no mount attempt with
no card, CYCLE start/stop, stream format. An independent review pass found nine issues
(CURRENT mode never re-arming after an SD failure, a 2 s core-0 freeze on an automatic
start with no card, buffer drops before ring drops, Settings SAVE not blocked while
recording, a 1-byte header margin, pre-reset rows at t = 0, a lost energy tick per reset,
a stale threshold-box colour, a shrinking drop count); all fixed before this commit.
**Not bench-verified.**

**Open:** scope GP1 (expect a 20 ms frame, 1000 µs after arming); confirm both
ESCs arm; card read-back with a card in the slot; one log per mode; whether 16 MHz SD
clock is reliable over the FPC; the unexplained "neither arms after power cycle" symptom if
it survives the timing fix (3.3 V signal level vs the WM150's, power-up order).

## v3.1c — 2026-09-08

### firmware: v3.1c — Test Mode, Settings, themes, V/I filter, touch harness

**Why:** v3.0 was confirmed on hardware (TP 200 Hz, V/T chips working), and four things
followed from bench use. The Calibrate screen only displayed compile-time constants and
did not earn a tile. Test Mode — ESC signal generation with configurable cycling — is the
board's namesake feature and was missing. The readings are noise-prone and needed a
tunable filter. And the theme did not read as dark, which turned into a long detour
(see the library entry below: the display was colour-inverted the whole time). On top of
that, touch responsiveness was reported fading after a minute or two of use, so the
build also carries the instrumentation to find out why.

**Changes**

- `EscOut.h/.cpp` — new. ESC servo signal on `PIN_ESC_SIG` (GP1, added to `Config.h`) via
  the RP2040 PWM slice: `analogWriteRange(periodUs)` makes the duty value the pulse width
  in microseconds. Manual set point 1000–2000 µs, frame period 5–20 ms, and a
  `millis()`-driven auto-cycle between two pulses with independent dwells. Output is a
  plain LOW pin until armed, disarming returns it to LOW rather than a zero-width frame,
  and any reset stops it.
- `Settings.h/.cpp` — new. Theme, filter index, ESC period and cycle parameters persisted
  by EEPROM emulation with magic, version, size and checksum, plus a range guard; anything
  implausible falls back to defaults. **Save is explicit** because an RP2040 flash commit
  halts both cores for a few ms. `settingsSave()` always stores the idle pulse regardless
  of the live set point, so a board never boots under throttle.
- `Sampler.h/.cpp` — linear weighted moving average (weights 1..N, newest heaviest) on V
  and I, selectable OFF/2/4/6/8/10/15/20 samples (26–263 ms at the 13.158 ms tick). Applied
  to the displayed numbers and the graph only. **Peaks stay raw**, screened by a
  physical-plausibility gate (≤160 A, ≤70 V) rather than a median: simulation showed
  median-of-3 discarding a genuine one-tick 100 A spike, which is exactly the event the
  instrument exists to catch. Energy integrates raw. Also core-1 health counters (tick
  overruns, longest tick).
- `Theme.h/.cpp` — the `COL_*` macros now dereference a runtime `Palette`, so switching
  theme is a pointer assignment plus repaint and no screen file changed. `PAL_DARK` is
  minimal-ink on pure black with bright borders; `PAL_CLASSIC` is navy built from real RGB
  (the v3.0 ground `0x0020` was the green LSB in RGB565, not blue).
- `ScreenTest.cpp` — new. Manual pulse with ±50/±10 steppers, frame period, cycle low/high
  pulse and dwells, start/stop. Status field doubles as the cycle countdown.
- `ScreenSettings.cpp` — new. Theme toggle, filter stepper (sample count in the box,
  window in ms on the caption line), calibration dump to serial (what `ScreenCal.cpp` used
  to draw — that file is deleted), Save, Dev Mode.
- `ScreenHome.cpp` — tiles are Live · Graph / Log · Test Mode · Settings; Dev moves under
  Settings. `ScreenDev.cpp` — theme toggle, the harness readouts (tap latency avg/max,
  worst frame, per-register drift, service max, tick health, `TP STALL` marker), and the
  crosshair now restores the chrome it drags across instead of scraping it.
- `Widgets.h/.cpp` — `drawStepBtn` draws +/- as filled bars sized from the box (a 5x7 glyph
  in a 44 px button was a speck), `drawStepperBox`, `t5Centered`, and the 4 px amber
  `escBar` along the top edge of every screen while the output is armed — placed there
  because every screen already spends its top corners and a motor warning must never be
  crowded out. Painted centrally in `paintScreen()` so a toast repaint cannot lose it.
- `Layout.h` — targets for Test (17) and Settings (7), a second Dev target, ESC bar height.
  `checkTargetOverlaps()` covers all six screens; verified offline as well: no hit-rect
  overlaps, no visual-rect overlaps, nothing off-screen.
- `RussoOne13/16/22.h` — regenerated. The originals had been built with a reduced
  character set: `+`, `/`, `,` and `%` were zero-width and silently dropped, which is why
  the step buttons first read "-50 -10 -10 -50". Regenerated with those glyphs; the
  parentheses were left out deliberately because they would have raised every font's
  height and shifted every layout. Heights are unchanged at 16/20/26.
- `Config.h` — `PIN_ESC_SIG 1`, `LCD_IPS 1`.
- `logging_current_meter_FABLE.ino` — settings load, theme apply, `escBegin()`, `escTick()`
  and `escBarTick()` in the loop, the once-per-second `[tp]` telemetry line (guarded by
  `availableForWrite()` so a stalled monitor can never block core 0), bench keys `d t L f
  e` alongside `r`, and a `built <date> <time>` banner line because every version in this
  project's history so far carries the same date.
- `README.md`, `firmware/README.md` — feature list, module layout, serial keys, the IPS
  inversion note, and the junction recipe for the library.

**Verification:** `arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero` against
arduino-pico 6.1.0: 102,828 bytes flash (4%), 17,160 bytes RAM (6%). Flashed and
bench-tested through v3.1b on the device: dark theme confirmed good, TP 200 Hz, UI 45 kHz,
tap latency 0 ms, service max 252 µs, one tick overrun in 39 minutes with a 7.9 ms worst
tick against a 13.2 ms budget. Test Mode ESC output not yet scoped. v3.1c itself (per-register
drift, navy palette, Test Mode re-spacing, crosshair repair) compiles clean and is not yet
flashed.

**Notes:** The touch fade has a lead. The register watchdog in this build found the
FT6336U's mode register `0xA4` not holding its value 47 times in 39 minutes and rewrote it
each time — consistent with the fade being controller state drift, and with the report that
touch "works a lot better" with the watchdog in. The per-register counters added here are
what will say whether all four registers move together (the part resetting — power or ESD on
the FPC) or A4 alone. Design decisions taken for v3.2 and not yet built: an ESC slider,
tap-to-jump anywhere; swipe-back recognised only when the swipe starts in dead space, so
buttons keep firing on the press edge.

### JCR_TouchScreen 1.1.0: fix IPS colour inversion, add controller watchdog and reinit

**Why:** Every colour this driver produced from v2.0 to v3.1a displayed as its
complement, and nobody noticed because a dark-on-black UI inverted to a light one that
merely looked "washed out". The old Arduino_GFX firmware constructed the panel with
`ips = true`, which sends `INVON`; this driver's init table ended with `INVOFF`. Every
palette judgement in that period was made on the inverted image. Separately, the
touch-fade investigation needed the driver to report on the controller's own health.

**Changes**

- `src/JCR_ST7796.h` — constructor gains `bool ips = true`; the init table ends with `0x21`
  when set, `0x20` otherwise. Default on because the module the library is documented
  against is IPS; TN panels pass false. Header comment explains the symptom.
- `src/JCR_FT6336.h/.cpp` — a register watchdog on the servicing core: every 500 ms it
  reads back `0x00`, `0x86`, `0x88`, `0xA4`, and any that no longer holds what `begin()`
  wrote is counted (`regDrift`, `driftByReg[4]`, `lastDriftReg`) and rewritten. Per-sample
  transaction timing (`serviceUsMax`, cleared per window by `resetServiceMax()`).
  `requestReinit()` re-runs `begin()` on the servicing core from a request flag, with the
  I2C bus brought up only once so a reinit cannot trip the core's pin-reassignment panic.
- `README.md` — IPS inversion section; the three new counters documented.
- `library.properties` — 1.1.0.

**Verification:** All three examples compile clean (70,076 / 70,492 / 70,644 bytes). On
hardware, the inversion fix is confirmed — black is black — and the watchdog produced its
first data (47 drifts on `0xA4` in 39 minutes, see the firmware entry above).


## v3.0 — 2026-09-08

### firmware: extract JCR_TouchScreen library and rebuild the UI as modules

**Why:** Touch had been unreliable across every earlier build — small targets
needed two or three presses, and touch appeared to die after a period of idling.
The cause turned out not to be the panel. Three things compounded: the FT6336U
ships in trigger mode, where `CTP_INT` only pulses on state *changes*, so a short
tap can be missed outright; touch was read from the drawing loop, so any slow
frame stretched the interval between reads until brief contacts landed and lifted
unseen; and monitor mode, enabled by default, drops the scan rate after ~30 s
idle. A minimal test firmware (`FABLE_DEV_TEST_SCREEN`) was written first to prove
the fix in isolation before the UI was rebuilt on it.

**Changes**

- `libraries/JCR_TouchScreen/` — new, 18 files. The reusable half of the old
  monolith: a raw-SPI ST7796 driver, the dual-core FT6336U touch engine and a
  bitmap text renderer, with no third-party graphics dependency.
  - `JCR_TFT.h/.cpp` — resolution- and rotation-agnostic base. Width and height
    are runtime state derived from the panel's native size and rotation, the
    scanline buffer is heap-allocated at `begin()` to the longest edge, and every
    primitive clips against live dimensions rather than compile-time macros.
  - `JCR_ST7796.h` — controller specifics behind three overrides (`writeInit`,
    `nativeSize`, `madctlFor`), so another controller or resolution is a small
    subclass rather than a fork. `rotationOffset()` exists for panels whose
    visible area is inset in the controller's address space.
  - `JCR_FT6336.h/.cpp` — the touch engine. Polling mode (`0xA4=0x00`), monitor
    mode forbidden (`0x86=0x00`), fastest scan period (`0x88=0x04`), fixed 200 Hz
    cadence, two-sample release confirmation, and full glitch counters. Events
    move through a lock-free single-producer ring inside the driver rather than
    the RP2040 hardware FIFO, which leaves that FIFO free for application use and
    lets touch be serviced from either core. **The library never claims a core** —
    `service()` is the primitive and the application decides where it runs.
  - `JCR_Text.h/.cpp` — one renderer for fixed-pitch and proportional bitmap
    fonts with integer scaling, replacing two near-duplicate renderers.
  - `fonts/Font5x7.*` — drawn glyph by glyph for this library, so it carries the
    same MIT licence as the code and no third-party font is redistributed.
  - `extras/make_fonts.py` — TTF to bitmap generator. `--mono` produces tabular
    figures, which is what makes a numeric field's cached redraw exact.
  - Three examples: `HelloScreen` (wiring proof), `DualCoreSampling` (core 1 doing
    its own periodic work *and* servicing touch, with a BLOCK button that stalls
    core 0 to demonstrate touch surviving it), `TouchReliabilityTest` (crosshair,
    16 px targets, every counter).
- `firmware/logging_current_meter_FABLE/` — the UI, rebuilt from one 2,360-line
  sketch into 21 files. The `.ino` is now ~190 lines of wiring; `Config.h` holds
  pins, bus rates and the calibration constants; `Layout.h` holds every pixel
  coordinate; `Sampler.*` the core-1 ADC and graph ring; `Widgets.*` the chrome
  and cached fields; `Screens.*` plus one `Screen*.cpp` per screen.
- `firmware/_archive/logging_current_meter_FABLE_v2.1.ino.txt` — the last
  bench-verified monolith, kept as a rollback. Deliberately not a `.ino` so the
  Arduino toolchain ignores it.
- `README.md` — repository layout gained `firmware/`, `libraries/` and this file;
  the "real firmware is not started" bullet replaced, since it is.
- `firmware/README.md` — the sketch table listed only `display_bringup`; it now
  covers all five sketches and the archive. Added a section on the new UI: the
  library dependency, the reasoning behind the dual-core touch arrangement, the
  four rules that keep it working, and the module layout. Corrected the toolchain
  section, which implied every sketch needs Arduino_GFX — the new build needs no
  third-party graphics library at all.
- `.gitignore` — excludes `Claude outputs/`, a working-session staging folder
  holding duplicate copies of files that also live in `firmware/` and
  `libraries/`, plus artefacts from unrelated tasks.

**Verification:** `arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero`
against arduino-pico 6.1.0, run in a Linux sandbox rather than on the development
machine. The sketch builds clean at 91,484 bytes flash (4%) and 16,344 bytes RAM
(6%); all three library examples build clean at 69,700 / 70,084 / 70,228 bytes.
**Not yet flashed or bench-tested on hardware** — v2.1 remains the last build
confirmed working on the device.

**Notes:** The refactor was reviewed line by line against v2.1 for transcription
drift; the ADC maths, formatters, graph autoscale and rendering, every layout
rect, and the dispatch and toast strings all match. That review also caught four
real defects, all fixed here: the `CTP_INT` edge counter was never re-attached
after the extraction, `resetStats()` was writing counters across cores instead of
raising a request on the owning core, a partially overhanging glyph cell could
program an address window the controller clamps and corrupt the display, and an
unmapped glyph was dropped without advancing the pen.

Two known cosmetic quirks, both accepted: the Dev Mode crosshair scrapes slivers
of static chrome while dragging (cleared on screen re-entry, not worth a repaint
per frame), and the 5x7 font is uppercase-only so "Wh"/"mA" render as "WH"/"MA".

`libraries/JCR_TouchScreen` must be copied or symlinked into
`Documents\Arduino\libraries\` before the sketch will build. It is staged inside
this repository so it is version-controlled alongside the project; if it is ever
published on its own, the README, LICENSE and `library.properties` are already
written for a standalone repository.
