#!/usr/bin/env python3
"""
capture_stream.py - record the Logging Current Meter's USB CSV stream on a PC.

    pip install pyserial
    python capture_stream.py                 # auto-detects the RP2040 port
    python capture_stream.py --port COM7     # or name it
    python capture_stream.py --out run1.csv

Writes the CSV header once, then every data row, to a timestamped file (or
--out). Lines starting with '#' are the meter's comments (banner, [tp]
telemetry, [log] events): they are echoed to the console, and kept in the
file only with --keep-comments. A pc_time column (seconds since capture start,
PC clock) is added so rows can be lined up with anything else you recorded.

Streaming is toggled on the meter by the 's' key. If no data row arrives
within 1.5 s of opening the port, this script sends 's' once to turn it on,
and sends 's' again on exit to turn it back off. If the stream was already on
(saved on the LOG screen), it is left alone.

Stop with Ctrl+C.
"""
import argparse
import datetime as dt
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is not installed:  pip install pyserial")

RP2040_VID = 0x2E8A


def find_port():
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == RP2040_VID]
    if not ports:
        sys.exit("No RP2040 serial port found - plug the meter in, or pass --port COMx")
    if len(ports) > 1:
        print("# several RP2040 ports found, using", ports[0].device, "- pass --port to choose")
    return ports[0].device


def is_data(line):
    return bool(line) and (line[0].isdigit() or line[0] == "-")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port, e.g. COM7 (default: auto-detect)")
    ap.add_argument("--out", help="output CSV (default: meter_YYYYmmdd_HHMMSS.csv)")
    ap.add_argument("--keep-comments", action="store_true", help="also write '#' lines to the file")
    ap.add_argument("--quiet", action="store_true", help="do not echo '#' lines to the console")
    args = ap.parse_args()

    port = args.port or find_port()
    out_name = args.out or dt.datetime.now().strftime("meter_%Y%m%d_%H%M%S.csv")

    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = True                     # the meter only talks once DTR is up
    print(f"# capturing {port} -> {out_name}   (Ctrl+C to stop)")

    t0 = time.monotonic()
    rows = 0
    header_written = False
    we_enabled = False
    last_status = 0.0
    buf = b""

    with open(out_name, "w", newline="") as f:
        try:
            while True:
                chunk = ser.read(4096)
                now = time.monotonic()
                if not chunk:
                    if rows == 0 and not we_enabled and now - t0 > 1.5:
                        ser.write(b"s")
                        we_enabled = True
                        print("# no data yet - sent 's' to start the stream")
                    continue
                buf += chunk
                *lines, buf = buf.split(b"\n")
                for raw in lines:
                    line = raw.decode("ascii", "replace").strip()
                    if not line:
                        continue
                    if line.startswith("#"):
                        if not args.quiet:
                            print(line)
                        if args.keep_comments:
                            f.write(line + "\n")
                    elif line.startswith("t_ms"):
                        if not header_written:
                            f.write("pc_time," + line + "\n")
                            header_written = True
                    elif is_data(line):
                        if not header_written:   # joined mid-stream: header was missed
                            f.write("pc_time,t_ms,i_raw,i_filt,v_raw,v_filt,w,t_c,esc_us\n")
                            header_written = True
                        f.write(f"{now - t0:.3f},{line}\n")
                        rows += 1
                if now - last_status > 5:
                    last_status = now
                    f.flush()
                    print(f"# {rows} rows, {now - t0:.0f} s")
        except KeyboardInterrupt:
            pass
        finally:
            if we_enabled:
                try:
                    ser.write(b"s")
                except serial.SerialException:
                    pass
            ser.close()
    print(f"# wrote {rows} rows to {out_name}")


if __name__ == "__main__":
    main()
