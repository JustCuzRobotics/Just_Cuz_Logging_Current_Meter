#!/usr/bin/env python3
"""
JCR Logging Current Meter - Log Analyzer
========================================

Graphs the SD-card logs written by the Logging Current Meter (firmware v3.2+).

    pip install pandas numpy matplotlib seaborn
    python log_analyzer.py                 # opens the GUI
    python log_analyzer.py <file-or-folder> # no GUI: analyze and exit

GUI
  Open Log File(s)...   pick one or more LOG_*.CSV files
  Open Folder...        every .CSV in that folder
  Either one analyzes everything straight away. Afterwards, tick 2-3 logs in
  the list and press "Compare Selected" for an overlay of just those runs.

Output goes to a "Log Graphs" folder beside the logs:
  <log name>-Current_Voltage_Temp.png   main chart + stats box
  <log name>-Energy.png                 mAh (left axis) and Wh (right axis)
  Log Summary.csv                       every stat, one column per log
  Log Summary-Comparison.png            bars across all logs (2+ logs)
  Compare-<a>_vs_<b>...-Overlay.png     the 2-3 logs you ticked

Log format (see firmware/README.md): '#' header lines, then
  t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,mah,wh,esc_us
and a closing '# end reason=...' line. t_ms < 0 is pre-trigger history.
Files captured over USB with capture_stream.py also load (they have no energy
columns, so energy is integrated from v_raw x i_raw instead).
"""

import os
import re
import sys
import subprocess
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")                      # render to files; the GUI is tkinter
import matplotlib.pyplot as plt
import seaborn as sns

GRAPH_DIR_NAME = "Log Graphs"
DPI = 300

# Categorical slots, fixed order (validated for colour-vision deficiency).
COL_CURRENT = "#2a78d6"    # blue
COL_VOLTAGE = "#eb6834"    # orange
COL_TEMP = "#1baf7a"       # aqua - always dashed, so it never relies on colour alone
RUN_COLORS = ["#2a78d6", "#eb6834", "#1baf7a"]   # compare: run 1, 2, 3
BAR_COLOR = "#2a78d6"

LOG_NAME_RE = re.compile(r"LOG_(\d+)_([A-Z]+)_([\d.]+)V", re.IGNORECASE)


def fmt_runtime(seconds):
    if seconds is None or not np.isfinite(seconds):
        return "-"
    tenths = int(round(max(seconds, 0.0) * 10))       # round first: 59.96 -> 1:00.0
    m, t = divmod(tenths, 600)
    return f"{m}:{t // 10:02d}.{t % 10}"


def short_name(stem):
    """LOG_07_CURRENT_22.4V -> LOG_07 for compare labels; anything else trimmed."""
    m = LOG_NAME_RE.search(stem)
    return f"LOG_{m.group(1)}" if m else stem[:24]


def open_file(path):
    """Open a file or folder with the OS default viewer."""
    try:
        if sys.platform.startswith("win"):
            os.startfile(path)                         # noqa: S606 (Windows only)
        elif sys.platform == "darwin":
            subprocess.Popen(["open", path])
        else:
            subprocess.Popen(["xdg-open", path])
    except Exception as e:                              # viewer missing: not fatal
        print(f"Could not open {path}: {e}")


def style_axes(ax):
    """Dyno-analyzer look: white, light grey grid, black border."""
    ax.set_facecolor("white")
    ax.grid(True, alpha=0.5, color="lightgrey", linewidth=0.5, zorder=0)
    for spine in ax.spines.values():
        spine.set_visible(True)
        spine.set_edgecolor("black")
        spine.set_linewidth(1.5)


class MeterLog:
    """One log file: parse, compute stats, draw its charts."""

    def __init__(self, path):
        self.path = Path(path)
        self.stem = self.path.stem
        self.df = None
        self.meta = {}
        self.end = {}
        self.metrics = {}

    # ------------------------------------------------------------------ load
    def load(self):
        header_lines = []
        with open(self.path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("#"):
                    header_lines.append(line[1:].strip())

        for h in header_lines:
            if h.startswith("JCR Logging Current Meter"):
                self.meta["firmware"] = h.replace("JCR Logging Current Meter", "").strip()
            elif h.startswith("file "):
                m = re.search(r"start=(.*?)\s{2,}", h)
                if m:
                    self.meta["start_cause"] = m.group(1).strip()
                for key in ("mode", "threshold", "duration", "rate"):
                    m = re.search(rf"{key}=(.*?)(?:\s{{2,}}|$)", h)
                    if m:
                        self.meta[key] = m.group(1).strip()
            elif h.startswith("filter="):
                self.meta["i_gain_nominal"] = "I gain NOMINAL" in h
                m = re.search(r"filter=(\d+)", h)
                if m:
                    self.meta["filter_samples"] = int(m.group(1))
            elif h.startswith("end "):
                for k, v in re.findall(r"(\w+)=(\S+)", h):
                    self.end[k] = v
            elif h.startswith("stream:"):
                self.meta["source"] = "usb stream"

        # Mode from the file name if the header is missing (e.g. a renamed copy).
        m = LOG_NAME_RE.search(self.stem)
        if "mode" not in self.meta and m:
            self.meta["mode"] = m.group(2).upper()

        df = pd.read_csv(self.path, comment="#", skipinitialspace=True)
        df.columns = [c.strip().lower() for c in df.columns]
        if "t_ms" not in df.columns:
            raise ValueError("no t_ms column - not a Logging Current Meter log")
        for c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
        # A log that was never stopped (power pulled, card removed) usually ends
        # in a half-written row, e.g. "19747,5.65,5.47,24.62,24.63," - its energy
        # columns are blank. Drop any row missing a core column so it can't turn
        # the totals into NaN, and remember how many went.
        core = [c for c in ("t_ms", "i_raw", "v_raw", "mah", "wh") if c in df.columns]
        n_before = len(df)
        df = df.dropna(subset=core).reset_index(drop=True)
        self.meta["dropped_rows"] = n_before - len(df)
        if df.empty:
            raise ValueError("no data rows")

        if "w" not in df.columns and {"i_raw", "v_raw"} <= set(df.columns):
            df["w"] = df["i_raw"] * df["v_raw"]

        # USB captures: t_ms is time since boot and there is no energy.
        if self.meta.get("source") == "usb stream" or "mah" not in df.columns:
            if df["t_ms"].iloc[0] > 0:
                df["t_ms"] = df["t_ms"] - df["t_ms"].iloc[0]
            self._integrate_energy(df)
            self.meta.setdefault("mode", "USB STREAM")

        for raw, filt in (("i_raw", "i_filt"), ("v_raw", "v_filt")):
            if filt not in df.columns and raw in df.columns:
                df[filt] = df[raw]
        df["t_s"] = df["t_ms"] / 1000.0
        self.df = df
        return self

    @staticmethod
    def _integrate_energy(df):
        t_h = df["t_ms"].to_numpy() / 3.6e6
        dt = np.diff(t_h, prepend=t_h[0])
        run = df["t_ms"].to_numpy() >= 0
        df["mah"] = np.cumsum(df["i_raw"].to_numpy() * 1000.0 * dt * run)
        df["wh"] = np.cumsum(df["w"].to_numpy() * dt * run)

    # --------------------------------------------------------------- metrics
    def compute_metrics(self):
        df = self.df
        run = df[df["t_ms"] >= 0]
        pre = df[df["t_ms"] < 0]
        if run.empty:
            run = df

        runtime_s = run["t_s"].max() - max(run["t_s"].min(), 0.0)
        mah = float(run["mah"].dropna().iloc[-1]) if run["mah"].notna().any() else 0.0
        wh = float(run["wh"].dropna().iloc[-1]) if run["wh"].notna().any() else 0.0

        i_peak_idx = run["i_raw"].idxmax()
        peak_i = float(run.loc[i_peak_idx, "i_raw"])
        peak_i_t = float(run.loc[i_peak_idx, "t_s"])
        peak_w = float(run["w"].max())

        # Resting voltage: the pre-trigger samples if the log has them, else the
        # first quarter second of the log.
        if len(pre) >= 3:
            v_rest = float(pre["v_raw"].median())
        else:
            first = run[run["t_s"] <= run["t_s"].min() + 0.25]["v_raw"]
            v_rest = float(first.median()) if len(first) else float(run["v_raw"].iloc[0])
        v_min = float(run["v_raw"].min())
        sag = max(v_rest - v_min, 0.0)

        t_peak = float(run["t_c"].max()) if "t_c" in run and run["t_c"].notna().any() else np.nan

        self.metrics = {
            "log_file": self.path.name,
            "mode": self.meta.get("mode", "?"),
            "start_cause": self.meta.get("start_cause", "-"),
            "runtime_s": round(runtime_s, 2),
            "runtime": fmt_runtime(runtime_s),
            "energy_mah": round(mah, 2),
            "energy_wh": round(wh, 4),
            "avg_current_a": round(mah * 3.6 / runtime_s, 2) if runtime_s > 0 else np.nan,
            "avg_power_w": round(wh * 3600 / runtime_s, 1) if runtime_s > 0 else np.nan,
            "peak_current_a": round(peak_i, 2),
            "peak_current_time_s": round(peak_i_t, 2),
            "peak_power_w": round(peak_w, 1),
            "resting_voltage_v": round(v_rest, 2),
            "min_voltage_v": round(v_min, 2),
            "voltage_sag_v": round(sag, 2),
            "peak_temp_c": round(t_peak, 1) if np.isfinite(t_peak) else np.nan,
            "threshold": self.meta.get("threshold", "-"),
            "duration_setting": self.meta.get("duration", "-"),
            "rate": self.meta.get("rate", "-"),
            "end_reason": self.end.get("reason", "INCOMPLETE (no end line)"),
            "rows": len(df),
            "dropped_partial_rows": self.meta.get("dropped_rows", 0),
            "ring_drops": self.end.get("ring_drops", "-"),
            "buffer_drops": self.end.get("buffer_drops", "-"),
            "i_gain_nominal": self.meta.get("i_gain_nominal", False),
            "firmware": self.meta.get("firmware", "-"),
        }
        return self.metrics

    def stats_text(self):
        m = self.metrics
        mode = m["mode"]
        if m["start_cause"] not in ("-", "") and m["start_cause"].lower() != mode.lower():
            mode = f"{mode} ({m['start_cause']})"
        lines = [
            f"Log mode: {mode}",
            f"Runtime: {m['runtime']}",
            f"Energy used: {m['energy_mah']:.1f} mAh / {m['energy_wh']:.2f} Wh",
            f"Peak current: {m['peak_current_a']:.1f} A @ {m['peak_current_time_s']:.1f} s",
            f"Peak power: {m['peak_power_w']:.0f} W",
            f"Voltage sag: {m['voltage_sag_v']:.2f} V "
            f"({m['resting_voltage_v']:.2f} → {m['min_voltage_v']:.2f} V)",
        ]
        if np.isfinite(m["peak_temp_c"]):
            lines.append(f"Peak temp: {m['peak_temp_c']:.1f} °C")
        if m["i_gain_nominal"]:
            lines.append("Note: current gain not calibrated")
        if m["end_reason"].startswith("INCOMPLETE"):
            extra = (f", {m['dropped_partial_rows']} partial row dropped"
                     if m["dropped_partial_rows"] == 1 else
                     f", {m['dropped_partial_rows']} partial rows dropped" if m["dropped_partial_rows"] else "")
            lines.append(f"Note: log incomplete (no end line{extra})")
        return "\n".join(lines)

    # ---------------------------------------------------------------- charts
    def _shade_pretrigger(self, ax):
        t0 = self.df["t_s"].min()
        if t0 < 0:
            ax.axvspan(t0, 0, color="0.6", alpha=0.18, zorder=0, label="Pre-trigger")

    def plot_main(self, out_dir):
        df = self.df
        t = df["t_s"]

        fig, ax_i = plt.subplots(figsize=(14, 8))
        fig.patch.set_facecolor("white")
        ax_v = ax_i.twinx()
        ax_t = ax_i.twinx()
        ax_t.spines["right"].set_position(("outward", 60))

        self._shade_pretrigger(ax_i)

        # Raw thin and faint (spikes stay visible), filtered bold on top.
        sns.lineplot(x=t, y=df["i_raw"], ax=ax_i, color=COL_CURRENT, linewidth=0.7,
                     alpha=0.35, estimator=None, sort=False, label="Current raw (A)", zorder=2)
        sns.lineplot(x=t, y=df["i_filt"], ax=ax_i, color=COL_CURRENT, linewidth=2.0,
                     estimator=None, sort=False, label="Current filtered (A)", zorder=3)
        sns.lineplot(x=t, y=df["v_raw"], ax=ax_v, color=COL_VOLTAGE, linewidth=0.7,
                     alpha=0.35, estimator=None, sort=False, label="Voltage raw (V)", zorder=2)
        sns.lineplot(x=t, y=df["v_filt"], ax=ax_v, color=COL_VOLTAGE, linewidth=2.0,
                     estimator=None, sort=False, label="Voltage filtered (V)", zorder=3)
        has_temp = "t_c" in df and df["t_c"].notna().any()
        if has_temp:
            sns.lineplot(x=t, y=df["t_c"], ax=ax_t, color=COL_TEMP, linewidth=2.0,
                         linestyle="--", estimator=None, sort=False, label="Temp (°C)", zorder=3)

        # Lay the three scales out in bands so the traces and the stats box
        # overlap as little as possible: current uses the bottom ~65 %,
        # voltage sits in a 40-65 % band, the top third is left for the box.
        i_max = max(float(df["i_raw"].max()), 1.0)
        ax_i.set_ylim(0, i_max / 0.65)

        v_lo, v_hi = float(df["v_raw"].min()), float(df["v_raw"].max())
        span = max((v_hi - v_lo) / 0.25, 1.0)
        base = v_lo - 0.40 * span
        ax_v.set_ylim(base, base + span)

        if has_temp:
            t_lo, t_hi = float(df["t_c"].min()), float(df["t_c"].max())
            tspan = max((t_hi - t_lo) / 0.25, 5.0)
            tbase = t_lo - 0.10 * tspan
            ax_t.set_ylim(tbase, tbase + tspan)
        else:
            ax_t.set_visible(False)

        ax_i.set_xlabel("Time (s)", fontsize=12, fontweight="bold")
        ax_i.set_ylabel("Current (A)", fontsize=12, fontweight="bold")
        ax_v.set_ylabel("Voltage (V)", fontsize=12, fontweight="bold")
        ax_t.set_ylabel("Temperature (°C)", fontsize=12, fontweight="bold")
        ax_i.set_xlim(t.min(), t.max())
        style_axes(ax_i)
        for ax in (ax_v, ax_t):
            ax.grid(False)
        ax_t.spines["right"].set_visible(True)
        ax_t.spines["right"].set_edgecolor("black")
        ax_t.spines["right"].set_linewidth(1.5)

        # One legend for all three axes (seaborn adds one per axes; merge them).
        handles, labels = [], []
        for ax in (ax_i, ax_v, ax_t):
            h, l = ax.get_legend_handles_labels()
            handles += h
            labels += l
            if ax.get_legend():
                ax.get_legend().remove()
        leg = ax_i.legend(handles, labels, loc="upper left", bbox_to_anchor=(0.01, 0.99),
                          frameon=True, fancybox=True, shadow=True, fontsize=9,
                          facecolor="white", edgecolor="black", framealpha=1.0, ncol=2)
        leg.set_zorder(10)

        ax_t.text(0.99, 0.98, self.stats_text(), transform=ax_i.transAxes, fontsize=10,
                  va="top", ha="right", multialignment="left", zorder=20,
                  bbox=dict(boxstyle="round,pad=0.5", facecolor="white", edgecolor="black", alpha=0.95))

        ax_i.set_title(f"Current / Voltage / Temperature - {self.stem}",
                       fontsize=14, fontweight="bold", pad=15)
        fig.subplots_adjust(left=0.07, bottom=0.09, right=0.86, top=0.92)

        out = Path(out_dir) / f"{self.stem}-Current_Voltage_Temp.png"
        fig.savefig(out, dpi=DPI, bbox_inches="tight")
        plt.close(fig)
        return out

    def plot_energy(self, out_dir):
        df = self.df
        t = df["t_s"]
        m = self.metrics

        fig, ax_mah = plt.subplots(figsize=(14, 6))
        fig.patch.set_facecolor("white")
        ax_wh = ax_mah.twinx()
        self._shade_pretrigger(ax_mah)

        sns.lineplot(x=t, y=df["mah"], ax=ax_mah, color=COL_CURRENT, linewidth=2.5,
                     estimator=None, sort=False, label="Energy (mAh)", zorder=3)
        sns.lineplot(x=t, y=df["wh"], ax=ax_wh, color=COL_VOLTAGE, linewidth=2.0,
                     linestyle="--", estimator=None, sort=False, label="Energy (Wh)", zorder=4)

        # Both scales start at 0 and share the same headroom, so the two lines
        # land on top of each other when the pack voltage is steady; where Wh
        # falls below mAh, the pack was sagging.
        top = 1.15
        ax_mah.set_ylim(0, max(float(df["mah"].max()), 1e-3) * top)
        ax_wh.set_ylim(0, max(float(df["wh"].max()), 1e-6) * top)

        t_end = float(t.max())
        ax_mah.set_xlabel("Time (s)", fontsize=12, fontweight="bold")
        ax_mah.set_ylabel("Energy used (mAh)", fontsize=12, fontweight="bold")
        ax_wh.set_ylabel("Energy used (Wh)", fontsize=12, fontweight="bold")
        ax_mah.set_xlim(t.min(), t_end)
        style_axes(ax_mah)
        ax_wh.grid(False)

        handles, labels = [], []
        for ax in (ax_mah, ax_wh):
            h, l = ax.get_legend_handles_labels()
            handles += h
            labels += l
            if ax.get_legend():
                ax.get_legend().remove()
        ax_mah.legend(handles, labels, loc="upper left", frameon=True, fancybox=True, shadow=True,
                      fontsize=9, facecolor="white", edgecolor="black", framealpha=1.0)

        summary = (f"Total: {m['energy_mah']:.1f} mAh / {m['energy_wh']:.2f} Wh\n"
                   f"Log mode: {m['mode']}   Runtime: {m['runtime']}\n"
                   f"Avg current: {m['avg_current_a']:.1f} A   Avg power: {m['avg_power_w']:.0f} W")
        ax_wh.text(0.99, 0.02, summary, transform=ax_mah.transAxes, fontsize=10, va="bottom",
                   ha="right", multialignment="left", zorder=20,
                   bbox=dict(boxstyle="round,pad=0.5", facecolor="white", edgecolor="black", alpha=0.95))

        ax_mah.set_title(f"Energy Used - {self.stem}", fontsize=14, fontweight="bold", pad=15)
        fig.subplots_adjust(left=0.07, bottom=0.11, right=0.92, top=0.9)
        out = Path(out_dir) / f"{self.stem}-Energy.png"
        fig.savefig(out, dpi=DPI, bbox_inches="tight")
        plt.close(fig)
        return out


# ====================================================================== batch
def graph_dir_for(paths):
    return Path(paths[0]).parent / GRAPH_DIR_NAME


def load_logs(paths, say=print):
    logs = []
    for p in paths:
        try:
            log = MeterLog(p).load()
            log.compute_metrics()
            logs.append(log)
        except Exception as e:
            say(f"  SKIPPED {Path(p).name}: {e}")
    return logs


def save_summary_csv(logs, out_dir):
    """Stats as rows, one column per log - the dyno analyzer's layout."""
    df = pd.DataFrame({log.path.name: pd.Series(log.metrics) for log in logs})
    out = Path(out_dir) / "Log Summary.csv"
    df.to_csv(out, index=True)
    return out


def plot_comparison_bars(logs, out_dir):
    names = [short_name(l.stem) for l in logs]
    fields = [("energy_mah", "Energy used (mAh)", "{:.0f}"),
              ("energy_wh", "Energy used (Wh)", "{:.2f}"),
              ("peak_current_a", "Peak current (A)", "{:.1f}"),
              ("voltage_sag_v", "Voltage sag (V)", "{:.2f}")]
    width = max(12, 1.1 * len(logs) + 4)
    fig, axes = plt.subplots(2, 2, figsize=(width, 9))
    fig.patch.set_facecolor("white")
    for ax, (key, label, fmt) in zip(axes.flat, fields):
        vals = [l.metrics[key] for l in logs]
        sns.barplot(x=names, y=vals, ax=ax, color=BAR_COLOR, width=0.6)
        for patch, v in zip(ax.patches, vals):
            ax.annotate(fmt.format(v), (patch.get_x() + patch.get_width() / 2, patch.get_height()),
                        ha="center", va="bottom", fontsize=8, xytext=(0, 2), textcoords="offset points")
        ax.set_title(label, fontsize=12, fontweight="bold")
        ax.set_ylabel(label)
        ax.set_xlabel("")
        ax.tick_params(axis="x", rotation=45 if len(logs) > 6 else 0, labelsize=9)
        ax.set_ylim(0, max(max(vals), 1e-6) * 1.15)
        style_axes(ax)
        ax.grid(axis="x", visible=False)

    # Mode + runtime under each name, so bars from different modes aren't
    # compared blindly.
    key_lines = [f"{short_name(l.stem)}: {l.metrics['mode']}, {l.metrics['runtime']}" for l in logs]
    fig.text(0.01, 0.005, "   |   ".join(key_lines), fontsize=8, va="bottom", wrap=True)
    fig.suptitle("Log Comparison", fontsize=16, fontweight="bold")
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    out = Path(out_dir) / "Log Summary-Comparison.png"
    fig.savefig(out, dpi=DPI, bbox_inches="tight")
    plt.close(fig)
    return out


def plot_compare_overlay(logs, out_dir):
    """2-3 logs on shared time axes: current, voltage, energy + a stats table."""
    fig = plt.figure(figsize=(16, 11))
    fig.patch.set_facecolor("white")
    gs = fig.add_gridspec(3, 2, width_ratios=[3.0, 1.3], hspace=0.12, wspace=0.08)
    ax_i = fig.add_subplot(gs[0, 0])
    ax_v = fig.add_subplot(gs[1, 0], sharex=ax_i)
    ax_e = fig.add_subplot(gs[2, 0], sharex=ax_i)
    ax_tbl = fig.add_subplot(gs[:, 1])
    ax_tbl.axis("off")

    for n, log in enumerate(logs):
        c = RUN_COLORS[n]
        df = log.df
        lab = short_name(log.stem)
        sns.lineplot(x=df["t_s"], y=df["i_filt"], ax=ax_i, color=c, linewidth=1.6,
                     estimator=None, sort=False, label=lab)
        sns.lineplot(x=df["t_s"], y=df["v_filt"], ax=ax_v, color=c, linewidth=1.6,
                     estimator=None, sort=False, label=lab)
        sns.lineplot(x=df["t_s"], y=df["mah"], ax=ax_e, color=c, linewidth=2.2,
                     estimator=None, sort=False, label=lab)

    t_min = min(l.df["t_s"].min() for l in logs)
    t_max = max(l.df["t_s"].max() for l in logs)
    for ax, ylab in ((ax_i, "Current, filtered (A)"), (ax_v, "Voltage, filtered (V)"),
                     (ax_e, "Energy used (mAh)")):
        if t_min < 0:
            ax.axvspan(t_min, 0, color="0.6", alpha=0.18, zorder=0)
        ax.set_ylabel(ylab, fontsize=11, fontweight="bold")
        style_axes(ax)
        ax.legend(loc="best", fontsize=9, frameon=True, facecolor="white", edgecolor="black", framealpha=1.0)
    ax_i.set_ylim(bottom=0)
    ax_e.set_ylim(bottom=0)
    ax_i.set_xlim(t_min, t_max)
    ax_e.set_xlabel("Time from log start (s)", fontsize=12, fontweight="bold")
    ax_i.set_xlabel("")
    ax_v.set_xlabel("")
    plt.setp(ax_i.get_xticklabels(), visible=False)
    plt.setp(ax_v.get_xticklabels(), visible=False)

    rows = [("Log mode", "mode", "{}"), ("Runtime", "runtime", "{}"),
            ("Energy (mAh)", "energy_mah", "{:.1f}"), ("Energy (Wh)", "energy_wh", "{:.2f}"),
            ("Avg current (A)", "avg_current_a", "{:.1f}"), ("Peak current (A)", "peak_current_a", "{:.1f}"),
            ("Peak power (W)", "peak_power_w", "{:.0f}"), ("Resting V", "resting_voltage_v", "{:.2f}"),
            ("Min V", "min_voltage_v", "{:.2f}"), ("Voltage sag (V)", "voltage_sag_v", "{:.2f}"),
            ("Peak temp (°C)", "peak_temp_c", "{:.1f}"), ("End", "end_reason", "{}")]
    cell_text = []
    for label, key, fmt in rows:
        row = []
        for log in logs:
            v = log.metrics.get(key)
            try:
                row.append(fmt.format(v) if not (isinstance(v, float) and np.isnan(v)) else "-")
            except (ValueError, TypeError):
                row.append(str(v))
        cell_text.append(row)
    cols = [short_name(l.stem) for l in logs]
    tbl = ax_tbl.table(cellText=cell_text, rowLabels=[r[0] for r in rows], colLabels=cols,
                       loc="upper center", cellLoc="center", bbox=[0.40, 0.35, 0.60, 0.6])
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(9)
    for (r, c), cell in tbl.get_celld().items():
        cell.set_edgecolor("black")
        if r == 0 and c >= 0:
            cell.set_text_props(fontweight="bold", color="white")
            cell.set_facecolor(RUN_COLORS[c])
    full = "\n".join(f"{short_name(l.stem)} = {l.path.name}" for l in logs)
    ax_tbl.text(0.0, 0.30, full, transform=ax_tbl.transAxes, fontsize=8, va="top")

    fig.suptitle("Run Comparison - " + " vs ".join(cols), fontsize=16, fontweight="bold", y=0.93)
    out = Path(out_dir) / ("Compare-" + "_vs_".join(cols) + "-Overlay.png")
    fig.savefig(out, dpi=DPI, bbox_inches="tight")
    plt.close(fig)
    return out


def analyze(paths, open_after=False, say=print):
    """Graphs for every log in `paths`; summary + comparison when there are 2+."""
    paths = [str(p) for p in paths]
    if not paths:
        return [], []
    out_dir = graph_dir_for(paths)
    out_dir.mkdir(exist_ok=True)
    say(f"Loading {len(paths)} log(s)...")
    logs = load_logs(paths, say)
    saved = []
    for i, log in enumerate(logs, 1):
        say(f"[{i}/{len(logs)}] {log.path.name}  {log.metrics['mode']}  {log.metrics['runtime']}  "
            f"{log.metrics['energy_mah']:.1f} mAh  peak {log.metrics['peak_current_a']:.1f} A")
        saved.append(log.plot_main(out_dir))
        saved.append(log.plot_energy(out_dir))
    overview = []
    if len(logs) > 1:
        overview.append(save_summary_csv(logs, out_dir))
        overview.append(plot_comparison_bars(logs, out_dir))
        say("Saved Log Summary.csv and Log Summary-Comparison.png")
    say(f"Graphs saved to: {out_dir}")

    if open_after and logs:
        # Opening dozens of images is useless; for a batch open the overview
        # chart and the folder instead.
        if len(logs) <= 3:
            for p in saved:
                open_file(str(p))
        else:
            open_file(str(overview[1]))
            open_file(str(out_dir))
    return logs, saved + overview


# ======================================================================== GUI
def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("JCR Logging Current Meter - Log Analyzer")
    root.geometry("820x600")

    state = {"logs": [], "paths": []}
    open_after = tk.BooleanVar(value=True)

    top = ttk.Frame(root, padding=8)
    top.pack(fill="x")
    src_label = ttk.Label(top, text="No logs loaded", foreground="#52514e")

    mid = ttk.Frame(root, padding=(8, 0))
    mid.pack(fill="both", expand=True)
    ttk.Label(mid, text="Logs (Ctrl/Shift-click to pick 2-3 for Compare):").pack(anchor="w")
    listbox = tk.Listbox(mid, selectmode="extended", font=("Consolas", 10), height=12)
    listbox.pack(fill="both", expand=True)

    status = tk.Text(root, height=10, font=("Consolas", 9), wrap="word")
    status.pack(fill="both", padx=8, pady=(4, 8))

    def say(msg):
        status.insert("end", msg + "\n")
        status.see("end")
        root.update_idletasks()

    def busy(on):
        root.config(cursor="watch" if on else "")
        root.update_idletasks()

    def refresh_list():
        listbox.delete(0, "end")
        for log in state["logs"]:
            m = log.metrics
            listbox.insert("end", f"{log.path.name:<34} {m['mode']:<8} {m['runtime']:>7}  "
                                  f"{m['energy_mah']:>8.1f} mAh  {m['peak_current_a']:>6.1f} A pk")

    def run_batch(paths, label):
        state["paths"] = paths
        src_label.config(text=label)
        busy(True)
        try:
            logs, _ = analyze(paths, open_after=open_after.get(), say=say)
            state["logs"] = logs
            refresh_list()
        except Exception as e:
            messagebox.showerror("Analysis failed", str(e))
            say(f"ERROR: {e}")
        finally:
            busy(False)

    def open_files():
        paths = filedialog.askopenfilenames(title="Select log file(s)",
                                            filetypes=[("Meter logs", "*.csv *.CSV"), ("All files", "*.*")])
        if paths:
            run_batch(list(paths), f"{len(paths)} file(s) in {Path(paths[0]).parent}")

    def open_folder():
        folder = filedialog.askdirectory(title="Select a folder of logs")
        if not folder:
            return
        paths = sorted(p for p in Path(folder).iterdir()
                       if p.is_file() and p.suffix.lower() == ".csv"
                       and p.name != "Log Summary.csv")
        if not paths:
            messagebox.showinfo("No logs", "No .CSV files in that folder.")
            return
        run_batch(paths, f"{len(paths)} log(s) in {folder}")

    def compare_selected():
        sel = listbox.curselection()
        if not (2 <= len(sel) <= 3):
            messagebox.showinfo("Compare", "Select 2 or 3 logs in the list first.")
            return
        logs = [state["logs"][i] for i in sel]
        out_dir = graph_dir_for([str(l.path) for l in logs])
        out_dir.mkdir(exist_ok=True)
        busy(True)
        try:
            out = plot_compare_overlay(logs, out_dir)
            say(f"Saved {out.name}")
            if open_after.get():
                open_file(str(out))
        except Exception as e:
            messagebox.showerror("Compare failed", str(e))
        finally:
            busy(False)

    def open_graph_folder():
        if state["paths"]:
            d = graph_dir_for(state["paths"])
            if d.exists():
                open_file(str(d))

    ttk.Button(top, text="Open Log File(s)...", command=open_files).pack(side="left")
    ttk.Button(top, text="Open Folder...", command=open_folder).pack(side="left", padx=6)
    ttk.Checkbutton(top, text="Open graphs after saving", variable=open_after).pack(side="left", padx=12)
    src_label.pack(side="left", padx=8)

    bottom = ttk.Frame(root, padding=(8, 0))
    bottom.pack(fill="x", before=status)
    ttk.Button(bottom, text="Compare Selected (2-3)", command=compare_selected).pack(side="left")
    ttk.Button(bottom, text="Open Graph Folder", command=open_graph_folder).pack(side="left", padx=6)

    say("Open a log file or a folder of logs to analyze.")
    root.mainloop()


def main():
    plt.style.use("seaborn-v0_8")
    sns.set_palette([COL_CURRENT, COL_VOLTAGE, COL_TEMP])
    args = sys.argv[1:]
    if not args:
        run_gui()
        return
    target = Path(args[0])
    if target.is_dir():
        paths = sorted(p for p in target.iterdir()
                       if p.is_file() and p.suffix.lower() == ".csv" and p.name != "Log Summary.csv")
    else:
        paths = [Path(a) for a in args]
    logs, _ = analyze(paths)
    if len(args) >= 2 and 2 <= len(logs) <= 3 and not target.is_dir():
        print("Saved", plot_compare_overlay(logs, graph_dir_for([str(l.path) for l in logs])).name)


if __name__ == "__main__":
    main()
